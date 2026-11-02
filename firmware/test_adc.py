from machine import ADC, Pin
from time import sleep_ms
from lcd import lcd_init, lcd_write_line

# Potentiometer connected to GP26 (ADC0)
pot = ADC(Pin(26))


def get_octave():
    # Average 8 ADC readings to reduce noise
    adc_value = 0

    for _ in range(8):
        adc_value += pot.read_u16()

    adc_value //= 8

    # Map ADC reading to octave 2 through 6
    octave = 2 + min(4, adc_value * 5 // 65536)

    return octave, adc_value


# Initialize LCD
lcd_init()
lcd_write_line(0, "Digital Synth")

previous_octave = None

while True:
    octave, adc_value = get_octave()

    if octave != previous_octave:
        lcd_write_line(1, "Octave: " + str(octave))
        previous_octave = octave

    print("ADC:", adc_value, "Octave:", octave)

    sleep_ms(100)
