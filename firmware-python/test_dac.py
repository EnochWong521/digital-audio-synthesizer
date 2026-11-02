
from machine import Pin, SPI
from time import sleep

# Same SPI configuration as previous lab
SPI_CS_PIN = 1
SPI_SCK_PIN = 2
SPI_MOSI_PIN = 3

spi = SPI(
    0,
    baudrate=1_000_000,
    polarity=0,
    phase=0,
    sck=Pin(SPI_SCK_PIN),
    mosi=Pin(SPI_MOSI_PIN)
)

cs = Pin(SPI_CS_PIN, Pin.OUT, value=1)

buf = bytearray(2)


def write_dac(value):
    value = max(0, min(1023, value))

    # Original lab command: update both DACs
    word = (0b1111 << 12) | (value << 2)

    buf[0] = (word >> 8) & 0xFF
    buf[1] = word & 0xFF

    cs.value(0)
    spi.write(buf)
    cs.value(1)


# Change this to 256, 512, or 768
DAC_VALUE = 512

write_dac(DAC_VALUE)

print("DAC output set to:", DAC_VALUE)

# Hold the output without additional SPI activity
while True:
    sleep(1)
