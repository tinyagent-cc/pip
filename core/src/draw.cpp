#include "pip/draw.hpp"
#include <algorithm>
#include <cstring>
namespace pip {
namespace {
// sin(deg) * 1024 for 0..90; the other quadrants come from symmetry. A table instead of
// <cmath> keeps the arcs integer-only, so a mouth redraw costs no float work per frame.
const int16_t SIN1024[91] = {
    0, 18, 36, 54, 71, 89, 107, 125, 143, 160, 178, 195, 213,
    230, 248, 265, 282, 299, 316, 333, 350, 367, 384, 400, 416, 433,
    449, 465, 481, 496, 512, 527, 543, 558, 573, 587, 602, 616, 630,
    644, 658, 672, 685, 698, 711, 724, 737, 749, 761, 773, 784, 796,
    807, 818, 828, 839, 849, 859, 868, 878, 887, 896, 904, 912, 920,
    928, 935, 943, 949, 956, 962, 968, 974, 979, 984, 989, 994, 998,
    1002, 1005, 1008, 1011, 1014, 1016, 1018, 1020, 1022, 1023, 1023, 1024, 1024,
};
int sin1024(int deg) {
    deg %= 360; if (deg < 0) deg += 360;
    if (deg <= 90) return SIN1024[deg];
    if (deg <= 180) return SIN1024[180 - deg];
    if (deg <= 270) return -SIN1024[deg - 180];
    return -SIN1024[360 - deg];
}
int cos1024(int deg) { return sin1024(deg + 90); }
int isqrt(long q) { if (q <= 0) return 0; int r = 0; while ((long)(r + 1) * (r + 1) <= q) ++r; return r; }

// Accumulates the painted extent so a returned rect is the real bounding box, not the
// generous one a shape's parameters would suggest (an arc covers far less than its circle).
struct Bounds {
    int x0 = 1 << 15, y0 = 1 << 15, x1 = -(1 << 15), y1 = -(1 << 15);
    void add(int x, int y, int pad) {
        x0 = std::min(x0, x - pad); y0 = std::min(y0, y - pad);
        x1 = std::max(x1, x + pad); y1 = std::max(y1, y + pad);
    }
    Rect rect() const {
        if (x1 < x0) return Rect{0, 0, 0, 0};
        return rect_clip(Rect{(int16_t)x0, (int16_t)y0, (int16_t)(x1 - x0 + 1), (int16_t)(y1 - y0 + 1)});
    }
};
void dot(Framebuffer& fb, int x, int y, int radius, uint16_t c) {
    if (radius <= 0) { if (x >= 0 && x < Framebuffer::W && y >= 0 && y < Framebuffer::H) fb.px[y * Framebuffer::W + x] = c; return; }
    fb.fill_ellipse(x, y, radius, radius, c);
}
// One round-rect shape in a single colour; the border is this shape drawn twice, once
// inset by a pixel.
void fill_round(Framebuffer& fb, Rect r, int radius, uint16_t c) {
    if (r.w <= 0 || r.h <= 0) return;
    if (radius < 0) radius = 0;
    int maxr = std::min((int)r.w, (int)r.h) / 2;
    if (radius > maxr) radius = maxr;
    int ytop = r.y + radius, ybot = r.y + r.h - 1 - radius;
    for (int y = r.y; y < r.y + r.h; ++y) {
        int dy = 0;
        if (y < ytop) dy = ytop - y; else if (y > ybot) dy = y - ybot;
        int hw = radius;
        if (dy) hw = isqrt((long)radius * radius - (long)dy * dy);
        int inset = radius - hw;
        fb.fill_rect(Rect{(int16_t)(r.x + inset), (int16_t)y, (int16_t)(r.w - 2 * inset), 1}, c);
    }
}
}

int text_width(const char* s, int scale) {
    size_t n = std::strlen(s);
    if (n == 0 || scale <= 0) return 0;
    return (int)(6 * scale * n) - scale;
}
Rect draw_text(Framebuffer& fb, int x, int y, const char* s, int scale, uint16_t colour) {
    if (scale <= 0 || !*s) return Rect{0, 0, 0, 0};
    for (int i = 0; s[i]; ++i) {
        const uint8_t* g = glyph(s[i]);
        int gx = x + i * 6 * scale;
        if (gx >= Framebuffer::W || gx + 5 * scale <= 0) continue;   // whole glyph off-screen
        for (int col = 0; col < 5; ++col)
            for (int row = 0; row < 7; ++row)
                if (g[col] & (1u << row))
                    fb.fill_rect(Rect{(int16_t)(gx + col * scale), (int16_t)(y + row * scale), (int16_t)scale, (int16_t)scale}, colour);
    }
    return rect_clip(Rect{(int16_t)x, (int16_t)y, (int16_t)text_width(s, scale), (int16_t)(7 * scale)});
}
Rect draw_disc(Framebuffer& fb, int cx, int cy, int r, uint16_t colour) {
    if (r < 0) return Rect{0, 0, 0, 0};
    dot(fb, cx, cy, r, colour);
    return rect_clip(Rect{(int16_t)(cx - r), (int16_t)(cy - r), (int16_t)(2 * r + 1), (int16_t)(2 * r + 1)});
}
Rect draw_line(Framebuffer& fb, int x0, int y0, int x1, int y1, uint16_t colour, int thickness) {
    int radius = thickness / 2;
    Bounds b;
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;   // dy is negative
    int err = dx + dy;
    for (;;) {
        dot(fb, x0, y0, radius, colour);
        b.add(x0, y0, radius);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
    return b.rect();
}
Rect draw_arc(Framebuffer& fb, int cx, int cy, int r, int start_deg, int end_deg, uint16_t colour, int thickness) {
    if (r <= 0 || end_deg < start_deg) return Rect{0, 0, 0, 0};
    int radius = thickness / 2;
    Bounds b;
    // Step 2 degrees, and always include the end angle so an arc never stops a step short.
    for (int a = start_deg;; a += 2) {
        if (a > end_deg) a = end_deg;
        int x = cx + (r * cos1024(a)) / 1024, y = cy + (r * sin1024(a)) / 1024;
        dot(fb, x, y, radius, colour);
        b.add(x, y, radius);
        if (a >= end_deg) break;
    }
    return b.rect();
}
Rect draw_round_rect(Framebuffer& fb, Rect r, int radius, uint16_t fill, uint16_t border) {
    if (r.w <= 0 || r.h <= 0) return Rect{0, 0, 0, 0};
    fill_round(fb, r, radius, border);
    if (fill != border) fill_round(fb, Rect{(int16_t)(r.x + 1), (int16_t)(r.y + 1), (int16_t)(r.w - 2), (int16_t)(r.h - 2)}, radius - 1, fill);
    return rect_clip(r);
}
}
