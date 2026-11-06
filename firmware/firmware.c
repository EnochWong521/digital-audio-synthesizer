
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <stdatomic.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/spi.h"
#include "hardware/adc.h"
#include "hardware/timer.h"

// ========================================
// PIN ASSIGNMENTS
// ========================================

#define DAC_CS       1
#define DAC_SCK      2
#define DAC_MOSI     3

#define WAVE_BUTTON  12
#define OCTAVE_ADC   26

#define LCD_RS       17
#define LCD_EN       18
#define LCD_D4       19
#define LCD_D5       20
#define LCD_D6       21
#define LCD_D7       22

static const uint NOTE_PINS[8] = {
    4, 5, 6, 7, 8, 9, 10, 11
};

// ========================================
// AUDIO SETTINGS
// ========================================

#define SAMPLE_RATE  50000
#define SAMPLE_US    20
#define TABLE_SIZE   256

#define SINE         0
#define SQUARE       1
#define TRIANGLE     2

static uint16_t sine_table[TABLE_SIZE];

// Shared between the two cores
static _Atomic uint32_t audio_step = 0;
static _Atomic uint32_t audio_waveform = SINE;
static _Atomic uint32_t audio_active = 0;

// Musical frequencies
static const float C_FREQ[5] = {
    65.4064f,    // C2
    130.8128f,   // C3
    261.6256f,   // C4
    523.2511f,   // C5
    1046.5023f   // C6
};

// C, D, E, F, G, A, B, next C
static const float NOTE_RATIO[8] = {
    1.0f,
    1.122462f,
    1.259921f,
    1.334840f,
    1.498307f,
    1.681793f,
    1.887749f,
    2.0f
};

static const char *NOTE_NAMES[8] = {
    "C", "D", "E", "F", "G", "A", "B", "C"
};

static const char *WAVE_NAMES[3] = {
    "SINE", "SQUARE", "TRI"
};

// ========================================
// LTC1661 DAC
// ========================================

static void write_dac(uint16_t value) {
    if (value > 1023) value = 1023;

    // Update DAC A and B
    uint16_t word = 0xF000 | (value << 2);

    uint8_t data[2] = {
        (uint8_t)(word >> 8),
        (uint8_t)(word & 0xFF)
    };

    gpio_put(DAC_CS, 0);
    spi_write_blocking(spi0, data, 2);
    gpio_put(DAC_CS, 1);
}

static void init_dac(void) {
    gpio_init(DAC_CS);
    gpio_set_dir(DAC_CS, GPIO_OUT);
    gpio_put(DAC_CS, 1);

    spi_init(spi0, 4000000);

    gpio_set_function(DAC_SCK, GPIO_FUNC_SPI);
    gpio_set_function(DAC_MOSI, GPIO_FUNC_SPI);

    spi_set_format(
        spi0, 8,
        SPI_CPOL_0,
        SPI_CPHA_0,
        SPI_MSB_FIRST
    );

    for (int i = 0; i < TABLE_SIZE; i++) {
        float angle = 2.0f * 3.14159265f
                      * i / TABLE_SIZE;

        sine_table[i] = (uint16_t)(
            512.0f + 511.0f * sinf(angle)
        );
    }

    write_dac(512);
}

// ========================================
// LCD DRIVER - 4-BIT MODE
// ========================================

static void lcd_nibble(uint8_t value) {
    gpio_put(LCD_D4, (value >> 0) & 1);
    gpio_put(LCD_D5, (value >> 1) & 1);
    gpio_put(LCD_D6, (value >> 2) & 1);
    gpio_put(LCD_D7, (value >> 3) & 1);

    sleep_us(1);
    gpio_put(LCD_EN, 1);
    sleep_us(2);
    gpio_put(LCD_EN, 0);
    sleep_us(2);
}

static void lcd_byte(uint8_t value, bool data) {
    gpio_put(LCD_RS, data ? 1 : 0);

    lcd_nibble(value >> 4);
    lcd_nibble(value & 0x0F);

    sleep_us(50);
}

static void lcd_command(uint8_t cmd) {
    lcd_byte(cmd, false);

    if (cmd == 0x01 || cmd == 0x02) {
        sleep_ms(2);
    }
}

static void lcd_data(uint8_t value) {
    lcd_byte(value, true);
}

static void lcd_init(void) {
    const uint pins[6] = {
        LCD_RS, LCD_EN, LCD_D4,
        LCD_D5, LCD_D6, LCD_D7
    };

    for (int i = 0; i < 6; i++) {
        gpio_init(pins[i]);
        gpio_set_dir(pins[i], GPIO_OUT);
        gpio_put(pins[i], 0);
    }

    sleep_ms(50);

    lcd_nibble(0x03);
    sleep_ms(5);

    lcd_nibble(0x03);
    sleep_us(150);

    lcd_nibble(0x03);
    sleep_us(150);

    lcd_nibble(0x02);
    sleep_us(150);

    lcd_command(0x28); // 4-bit, two lines
    lcd_command(0x0C); // Display on
    lcd_command(0x06); // Increment cursor
    lcd_command(0x01); // Clear display
}

static void lcd_write_line(int row, const char *text) {
    lcd_command(row == 0 ? 0x80 : 0xC0);

    size_t length = strlen(text);

    for (int i = 0; i < 16; i++) {
        char c = (i < length) ? text[i] : ' ';
        lcd_data((uint8_t)c);
    }
}

// ========================================
// CORE 1: INPUTS AND LCD
// ========================================

static void controls_core(void) {
    uint pins[9];

    for (int i = 0; i < 8; i++) {
        pins[i] = NOTE_PINS[i];
    }
    pins[8] = WAVE_BUTTON;

    uint8_t last_raw[9];
    uint8_t stable[9];
    uint32_t last_change[9];

    for (int i = 0; i < 9; i++) {
        gpio_init(pins[i]);
        gpio_set_dir(pins[i], GPIO_IN);
        gpio_pull_up(pins[i]);

        last_raw[i] = gpio_get(pins[i]);
        stable[i] = last_raw[i];
        last_change[i] = 0;
    }

    adc_init();
    adc_gpio_init(OCTAVE_ADC);
    adc_select_input(0);

    lcd_init();

    int octave = 4;
    int waveform = SINE;

    int previous_octave = -1;
    int previous_note = -2;
    int previous_waveform = -1;

    uint32_t last_adc_time = 0;

    while (true) {
        uint32_t now =
            to_ms_since_boot(get_absolute_time());

        // Debounce all nine buttons
        for (int i = 0; i < 9; i++) {
            uint8_t raw = gpio_get(pins[i]);

            if (raw != last_raw[i]) {
                last_raw[i] = raw;
                last_change[i] = now;
            }

            if (raw != stable[i] &&
                (uint32_t)(now - last_change[i]) >= 25) {

                stable[i] = raw;

                // Waveform button: react once per press
                if (i == 8 && raw == 0) {
                    waveform = (waveform + 1) % 3;
                }
            }
        }

        // Read octave potentiometer every 30 ms
        if ((uint32_t)(now - last_adc_time) >= 30) {
            uint32_t total = 0;

            for (int i = 0; i < 8; i++) {
                total += adc_read();
            }

            uint32_t value = total / 8;

            // 12-bit ADC: 0 to 4095
            octave = 2 + (value * 5 / 4096);

            if (octave > 6) octave = 6;

            last_adc_time = now;
        }

        // Monophonic: first pressed key wins
        int note = -1;

        for (int i = 0; i < 8; i++) {
            if (stable[i] == 0) {
                note = i;
                break;
            }
        }

        // Only update when a control changes
        if (note != previous_note ||
            octave != previous_octave ||
            waveform != previous_waveform) {

            uint32_t phase_step = 0;

            if (note >= 0) {
                double frequency =
                    C_FREQ[octave - 2] * NOTE_RATIO[note];

                phase_step = (uint32_t)(
                    frequency * 4294967296.0
                    / SAMPLE_RATE + 0.5
                );
            }

            atomic_store(&audio_step, phase_step);
            atomic_store(&audio_waveform, waveform);
            atomic_store(&audio_active, note >= 0);

            char line1[17];
            char line2[17];

            snprintf(
                line1, sizeof(line1),
                "OCT:%d %s",
                octave, WAVE_NAMES[waveform]
            );

            if (note >= 0) {
                int note_octave =
                    octave + (note == 7 ? 1 : 0);

                snprintf(
                    line2, sizeof(line2),
                    "NOTE:%s%d",
                    NOTE_NAMES[note],
                    note_octave
                );
            } else {
                snprintf(
                    line2, sizeof(line2),
                    "NOTE:--"
                );
            }

            lcd_write_line(0, line1);
            lcd_write_line(1, line2);

            previous_note = note;
            previous_octave = octave;
            previous_waveform = waveform;
        }

        sleep_ms(5);
    }
}

// ========================================
// CORE 0: AUDIO GENERATION
// ========================================


int main(void) {
    // Force Pico 2 regulator into PWM mode
    // to reduce 3.3V supply ripple
    gpio_init(23);
    gpio_set_dir(23, GPIO_OUT);
    gpio_put(23, 1);
    
    init_dac();

    multicore_launch_core1(controls_core);

    uint32_t phase = 0;
    uint32_t next_sample = time_us_32();

    bool was_playing = false;

    while (true) {
        // Maintain the 50 kHz timing schedule
        while ((int32_t)(
            time_us_32() - next_sample
        ) < 0) {
            tight_loop_contents();
        }

        next_sample += SAMPLE_US;

        bool active = atomic_load(&audio_active);
        uint32_t step = atomic_load(&audio_step);

        // No note pressed: stop updating the DAC
        if (!active || step == 0) {

            if (was_playing) {
                // Return output to midscale once
                write_dac(512);

                phase = 0;
                was_playing = false;
            }

            continue;
        }

        was_playing = true;

        uint32_t waveform =
            atomic_load(&audio_waveform);

        uint16_t sample;

        if (waveform == SINE) {
            uint8_t index = phase >> 24;
            sample = sine_table[index];

        } else if (waveform == SQUARE) {
            sample = (phase < 0x80000000u)
                     ? 1023 : 0;

        } else {
            uint16_t ramp = phase >> 22;

            if (ramp < 512) {
                sample = ramp * 2;
            } else {
                sample = (1023 - ramp) * 2;
            }
        }

        write_dac(sample);
        phase += step;
    }
}

