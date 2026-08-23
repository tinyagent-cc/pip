#pragma once
// Single source of truth for wiring. README's table is generated from this by hand; keep them equal.
namespace pip::pins {
constexpr unsigned SPI_SCK = 18, SPI_MOSI = 19, LCD_CS = 17, LCD_DC = 20, LCD_RST = 21;
constexpr unsigned I2C_SDA = 4, I2C_SCL = 5;
constexpr unsigned BUTTON = 15;
constexpr unsigned LED_R = 10, LED_G = 11, LED_B = 12;
constexpr unsigned I2S_BCLK = 26, I2S_LRCLK = 27, I2S_DIN = 28;
constexpr unsigned UART_TX = 0, UART_RX = 1;   // link to the Pi Zero brain (PL011 /dev/ttyAMA0, 921600)
}
