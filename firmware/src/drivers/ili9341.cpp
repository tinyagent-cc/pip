#include "drivers/ili9341.hpp"
#include <algorithm>
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "pins.hpp"
namespace pip::drv {
namespace {
struct Step { uint8_t cmd; uint8_t n; uint8_t d[15]; uint16_t delay_ms; };
// Standard ILI9341 bring-up (same sequence the Adafruit driver uses). MADCTL 0x28 = MV|BGR, landscape.
const Step kInit[] = {
    {0xEF, 3, {0x03, 0x80, 0x02}, 0}, {0xCF, 3, {0x00, 0xC1, 0x30}, 0}, {0xED, 4, {0x64, 0x03, 0x12, 0x81}, 0},
    {0xE8, 3, {0x85, 0x00, 0x78}, 0}, {0xCB, 5, {0x39, 0x2C, 0x00, 0x34, 0x02}, 0}, {0xF7, 1, {0x20}, 0},
    {0xEA, 2, {0x00, 0x00}, 0}, {0xC0, 1, {0x23}, 0}, {0xC1, 1, {0x10}, 0}, {0xC5, 2, {0x3E, 0x28}, 0},
    {0xC7, 1, {0x86}, 0}, {0x36, 1, {0x28}, 0}, {0x37, 1, {0x00}, 0}, {0x3A, 1, {0x55}, 0},
    {0xB1, 2, {0x00, 0x18}, 0}, {0xB6, 3, {0x08, 0x82, 0x27}, 0}, {0xF2, 1, {0x00}, 0}, {0x26, 1, {0x01}, 0},
    {0xE0, 15, {0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00}, 0},
    {0xE1, 15, {0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F}, 0},
    {0x11, 0, {}, 120}, {0x29, 0, {}, 20},
};
}
void Ili9341::init(spi_inst_t* spi, unsigned baud_hz) {
    spi_ = spi;
    spi_init(spi_, baud_hz);
    spi_set_format(spi_, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(pins::SPI_SCK, GPIO_FUNC_SPI);
    gpio_set_function(pins::SPI_MOSI, GPIO_FUNC_SPI);
    for (unsigned p : {pins::LCD_CS, pins::LCD_DC, pins::LCD_RST}) { gpio_init(p); gpio_set_dir(p, true); gpio_put(p, 1); }
    gpio_put(pins::LCD_RST, 0); sleep_ms(10); gpio_put(pins::LCD_RST, 1); sleep_ms(120);
    for (const Step& s : kInit) { cmd(s.cmd); if (s.n) data(s.d, s.n); if (s.delay_ms) sleep_ms(s.delay_ms); }
}
void Ili9341::cmd(uint8_t c) { gpio_put(pins::LCD_DC, 0); gpio_put(pins::LCD_CS, 0); spi_write_blocking(spi_, &c, 1); gpio_put(pins::LCD_CS, 1); }
void Ili9341::data(const uint8_t* d, size_t n) { gpio_put(pins::LCD_DC, 1); gpio_put(pins::LCD_CS, 0); spi_write_blocking(spi_, d, n); gpio_put(pins::LCD_CS, 1); }
void Ili9341::window(int x0, int y0, int x1, int y1) {
    uint8_t c[4] = {(uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1};
    uint8_t p[4] = {(uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1};
    cmd(0x2A); data(c, 4); cmd(0x2B); data(p, 4); cmd(0x2C);
}
void Ili9341::push(const Framebuffer& fb, Rect r) {
    int x0 = std::max(0, (int)r.x), y0 = std::max(0, (int)r.y);
    int x1 = std::min(Framebuffer::W, (int)r.x + (int)r.w), y1 = std::min(Framebuffer::H, (int)r.y + (int)r.h);
    if (x1 <= x0 || y1 <= y0) return;
    window(x0, y0, x1 - 1, y1 - 1);
    gpio_put(pins::LCD_DC, 1); gpio_put(pins::LCD_CS, 0);
    spi_set_format(spi_, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    for (int y = y0; y < y1; ++y) spi_write16_blocking(spi_, &fb.px[y * Framebuffer::W + x0], (size_t)(x1 - x0));
    spi_set_format(spi_, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_put(pins::LCD_CS, 1);
}
}
