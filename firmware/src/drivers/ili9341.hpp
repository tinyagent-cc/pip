#pragma once
#include "hardware/spi.h"
#include "pip/face.hpp"
namespace pip::drv {
// ILI9341 over SPI0, landscape 320x240, RGB565. Commands are 8-bit, pixels 16-bit.
class Ili9341 {
public:
    void init(spi_inst_t* spi, unsigned baud_hz);
    void push(const Framebuffer& fb, Rect r);   // pushes the rows of r from fb, blocking
private:
    void cmd(uint8_t c);
    void data(const uint8_t* d, size_t n);
    void window(int x0, int y0, int x1, int y1);
    spi_inst_t* spi_ = nullptr;
};
}
