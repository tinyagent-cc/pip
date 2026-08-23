#pragma once
#include <cstdint>
namespace pip {
struct Rect { int16_t x, y, w, h; bool empty() const { return w <= 0 || h <= 0; } };
Rect rect_union(Rect a, Rect b);
Rect rect_clip(Rect r);            // to the framebuffer; an off-screen rect comes back empty
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
// RGB565 landscape framebuffer, 320x240, 150 KB. Lives in .bss on the Pico 2 W (520 KB SRAM).
struct Framebuffer {
    static constexpr int W = 320, H = 240;
    uint16_t px[W * H];
    void fill(uint16_t c);
    void fill_rect(Rect r, uint16_t c);                             // clipped
    void fill_ellipse(int cx, int cy, int rx, int ry, uint16_t c);  // filled, clipped
    uint16_t at(int x, int y) const { return px[y * W + x]; }
};
}
