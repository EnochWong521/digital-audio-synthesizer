from machine import Pin, SPI
from time import ticks_us, ticks_add, ticks_diff
import micropython
import math

# ============================================
# SETTINGS
# ============================================

FREQUENCY = 262
NUM_SAMPLES = 16
WAVEFORM = "sine"  # sine, square, triangle

# ============================================
# SPI SETUP
# ============================================

spi = SPI(
    0,
    baudrate=4_000_000,
    polarity=0,
    phase=0,
    sck=Pin(2),
    mosi=Pin(3)
)

cs = Pin(1, Pin.OUT, value=1)

# Cache these functions to reduce lookup overhead
spi_write = spi.write
cs_set = cs.value

# ============================================
# GENERATE LOOKUP TABLE
# ============================================

samples = []

for i in range(NUM_SAMPLES):

    if WAVEFORM == "sine":
        value = int(
            (math.sin(2 * math.pi * i / NUM_SAMPLES) + 1)
            * 1023 / 2
        )

    elif WAVEFORM == "square":
        value = 1023 if i < NUM_SAMPLES // 2 else 0

    elif WAVEFORM == "triangle":
        half = NUM_SAMPLES // 2

        if i < half:
            value = 1023 * i // (half - 1)
        else:
            value = 1023 * (NUM_SAMPLES - 1 - i) // (half - 1)

    samples.append(value)

# ============================================
# PRECOMPUTE SPI PACKETS
# ============================================

packets = []

for value in samples:

    # Same LTC1661 command as previous lab
    word = (0b1111 << 12) | (value << 2)

    # Create the two bytes only once
    packet = bytes([
        (word >> 8) & 0xFF,
        word & 0xFF
    ])

    packets.append(packet)

# ============================================
# NATIVE-COMPILED OUTPUT LOOP
# ============================================

@micropython.native
def play_cycle(packets, deadline, base_us,
               remainder, sample_rate, state,
               write, select):

    error = state[0]

    for packet in packets:

        while ticks_diff(deadline, ticks_us()) > 0:
            pass

        select(0)
        write(packet)
        select(1)

        # Fractional sample timing
        interval = base_us
        error += remainder

        if error >= sample_rate:
            error -= sample_rate
            interval += 1

        deadline = ticks_add(deadline, interval)

    state[0] = error
    return deadline


# ============================================
# MAIN LOOP
# ============================================

sample_rate = FREQUENCY * NUM_SAMPLES

base_us = 1_000_000 // sample_rate
remainder = 1_000_000 % sample_rate

# Preserve fractional timing between cycles
timing_state = [0]

print("Target frequency:", FREQUENCY, "Hz")
print("Samples per cycle:", NUM_SAMPLES)

next_sample_time = ticks_us()

while True:
    next_sample_time = play_cycle(
        packets,
        next_sample_time,
        base_us,
        remainder,
        sample_rate,
        timing_state,
        spi_write,
        cs_set
    )
