#pragma once
#include <cstdint>
#include "pip/fb.hpp"
#include "pip/font5x7.hpp"
namespace pip {
// Every primitive clips to the framebuffer and returns the rectangle it actually touched,
// so callers can union them into one dirty rect and push nothing else.
Rect draw_text(Framebuffer& fb, int x, int y, const char* s, int scale, uint16_t colour);   // advance 6*scale per char, no wrap
int  text_width(const char* s, int scale);                                                  // 6*scale*len - scale (no trailing gap)
Rect draw_line(Framebuffer& fb, int x0, int y0, int x1, int y1, uint16_t colour, int thickness);  // Bresenham; each point a disc of radius thickness/2
Rect draw_disc(Framebuffer& fb, int cx, int cy, int r, uint16_t colour);
Rect draw_arc(Framebuffer& fb, int cx, int cy, int r, int start_deg, int end_deg, uint16_t colour, int thickness);  // degrees clockwise from 3 o'clock (y down), step 2
Rect draw_round_rect(Framebuffer& fb, Rect r, int radius, uint16_t fill, uint16_t border);  // 1 px border; fill == border -> solid
}
