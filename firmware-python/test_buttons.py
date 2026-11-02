from machine import Pin, ADC
from time import sleep_ms, ticks_ms, ticks_diff
from lcd import lcd_init, lcd_write_line

# Note buttons
NOTE_NAMES = ["C", "D", "E", "F", "G", "A", "B", "C+"]
NOTE_PINS = [4, 5, 6, 7, 8, 9, 10, 11]

buttons = [
    Pin(gpio, Pin.IN, Pin.PULL_UP)
    for gpio in NOTE_PINS
]

# Waveform selection button
wave_button = Pin(12, Pin.IN, Pin.PULL_UP)
buttons.append(wave_button)

waveforms = ["SINE", "SQUARE", "TRI"]
wave_index = 0

# Octave potentiometer
pot = ADC(Pin(26))

# Debouncing state
stable = [pin.value() for pin in buttons]
last_raw = stable[:]
last_change = [ticks_ms()] * len(buttons)


def get_octave():
    value = pot.read_u16()
    return 2 + min(4, value * 5 // 65536)


def read_buttons():
    global wave_index

    now = ticks_ms()

    for i, pin in enumerate(buttons):
        raw = pin.value()

        if raw != last_raw[i]:
            last_raw[i] = raw
            last_change[i] = now

        if raw != stable[i]:
            if ticks_diff(now, last_change[i]) >= 25:
                stable[i] = raw

                # Waveform button pressed
                if i == 8 and raw == 0:
                    wave_index = (wave_index + 1) % 3


def get_pressed_notes(octave):
    notes = []

    for i in range(8):
        if stable[i] == 0:
            if i == 7:
                notes.append("C" + str(octave + 1))
            else:
                notes.append(NOTE_NAMES[i] + str(octave))

    return notes


lcd_init()
previous_display = None

while True:
    read_buttons()

    octave = get_octave()
    notes = get_pressed_notes(octave)
    waveform = waveforms[wave_index]

    line1 = "OCT:" + str(octave) + " " + waveform
    line2 = "NOTES:" + (" ".join(notes) if notes else "-")

    display = (line1, line2)

    if display != previous_display:
        lcd_write_line(0, line1)
        lcd_write_line(1, line2)
        print("Pressed:", notes, "Waveform:", waveform)
        previous_display = display

    sleep_ms(10)
