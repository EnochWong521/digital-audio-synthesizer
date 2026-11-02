from machine import Pin
from time import sleep_ms, sleep_us

# LCD pin configuration
LCD_RS = Pin(17, Pin.OUT)
LCD_EN = Pin(18, Pin.OUT)

LCD_D4 = Pin(19, Pin.OUT)
LCD_D5 = Pin(20, Pin.OUT)
LCD_D6 = Pin(21, Pin.OUT)
LCD_D7 = Pin(22, Pin.OUT)

DATA_PINS = [LCD_D4, LCD_D5, LCD_D6, LCD_D7]


def lcd_pulse_enable():
    LCD_EN.value(0)
    sleep_us(1)
    LCD_EN.value(1)
    sleep_us(2)
    LCD_EN.value(0)
    sleep_us(1)


def lcd_write_nibble(nibble):
    for i in range(4):
        DATA_PINS[i].value((nibble >> i) & 1)

    lcd_pulse_enable()


def lcd_send(value, rs):
    LCD_RS.value(rs)

    # Send upper 4 bits first
    lcd_write_nibble(value >> 4)

    # Send lower 4 bits
    lcd_write_nibble(value & 0x0F)

    sleep_us(50)


def lcd_command(cmd):
    lcd_send(cmd, 0)

    if cmd == 0x01 or cmd == 0x02:
        sleep_ms(2)


def lcd_data(data):
    lcd_send(data, 1)


def lcd_init():
    LCD_RS.value(0)
    LCD_EN.value(0)

    sleep_ms(50)

    # Initialize LCD in 4-bit mode
    lcd_write_nibble(0x03)
    sleep_ms(5)

    lcd_write_nibble(0x03)
    sleep_us(200)

    lcd_write_nibble(0x03)
    sleep_us(200)

    lcd_write_nibble(0x02)
    sleep_us(200)

    lcd_command(0x28)  # 4-bit, 2 lines, 5x8 font
    lcd_command(0x08)  # Display off
    lcd_command(0x01)  # Clear display
    lcd_command(0x06)  # Entry mode
    lcd_command(0x0C)  # Display on, cursor off


def lcd_print(text):
    for char in text:
        lcd_data(ord(char))


def lcd_set_cursor(row, col):
    address = (0x00 if row == 0 else 0x40) + col
    lcd_command(0x80 | address)


def lcd_write_line(row, text):
    lcd_set_cursor(row, 0)

    text = text[:16]
    text = text + " " * (16 - len(text))

    lcd_print(text)


# -------------------------
# Main program
# -------------------------

if __name__ == "__main__":
    lcd_init()
    lcd_write_line(0, "Electronic Synth")
    lcd_write_line(1, "LCD Test OK")