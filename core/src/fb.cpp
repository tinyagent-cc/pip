#include "pip/fb.hpp"
#include <algorithm>
namespace pip {
Rect rect_union(Rect a, Rect b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    int16_t x0 = std::min(a.x, b.x), y0 = std::min(a.y, b.y);
    int16_t x1 = std::max((int16_t)(a.x + a.w), (int16_t)(b.x + b.w)), y1 = std::max((int16_t)(a.y + a.h), (int16_t)(b.y + b.h));
    return Rect{x0, y0, (int16_t)(x1 - x0), (int16_t)(y1 - y0)};
}
Rect rect_clip(Rect r) {
    int x0 = std::max(0, (int)r.x), y0 = std::max(0, (int)r.y);
    int x1 = std::min(Framebuffer::W, (int)r.x + (int)r.w), y1 = std::min(Framebuffer::H, (int)r.y + (int)r.h);
    if (x1 <= x0 || y1 <= y0) return Rect{0, 0, 0, 0};
    return Rect{(int16_t)x0, (int16_t)y0, (int16_t)(x1 - x0), (int16_t)(y1 - y0)};
}
void Framebuffer::fill(uint16_t c) { for (int i = 0; i < W * H; ++i) px[i] = c; }
void Framebuffer::fill_rect(Rect r, uint16_t c) {
    int x0 = std::max(0, (int)r.x), y0 = std::max(0, (int)r.y);
    int x1 = std::min(W, (int)r.x + (int)r.w), y1 = std::min(H, (int)r.y + (int)r.h);
    for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) px[y * W + x] = c;
}
void Framebuffer::fill_ellipse(int cx, int cy, int rx, int ry, uint16_t c) {
    if (rx <= 0 || ry <= 0) return;
    for (int dy = -ry; dy <= ry; ++dy) {
        int y = cy + dy; if (y < 0 || y >= H) continue;
        // half-width at this row: rx * sqrt(1 - (dy/ry)^2), in integer math
        long num = (long)rx * rx * ((long)ry * ry - (long)dy * dy);
        long den = (long)ry * ry;
        int hw = 0; long q = num / den; while ((long)(hw + 1) * (hw + 1) <= q) ++hw;
        int x0 = std::max(0, cx - hw), x1 = std::min(W - 1, cx + hw);
        for (int x = x0; x <= x1; ++x) px[y * W + x] = c;
    }
}
}
