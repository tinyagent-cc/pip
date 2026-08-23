#include <cstring>
#include "check.h"
#include "pip/font5x7.hpp"
#include "pip/draw.hpp"
using namespace pip;
static Framebuffer fb;
static void run() {
    CHECK_EQ(glyph('A')[0], (uint8_t)0x7E); CHECK_EQ(glyph('A')[4], (uint8_t)0x7E); CHECK_EQ(glyph('A')[2], (uint8_t)0x11);
    CHECK_EQ(glyph('I')[2], (uint8_t)0x7F); CHECK_EQ(glyph(' ')[2], (uint8_t)0);
    CHECK_EQ(glyph('\x01')[0], glyph('?')[0]);  // out of range -> '?'
    fb.fill(0);
    Rect r = draw_text(fb, 10, 10, "I", 1, 0xFFFF);
    CHECK_EQ(r.x, (int16_t)10); CHECK_EQ(r.y, (int16_t)10); CHECK_EQ(r.w, (int16_t)5); CHECK_EQ(r.h, (int16_t)7);
    CHECK_EQ(fb.at(12, 10), (uint16_t)0xFFFF);  // the stem, top row
    CHECK_EQ(fb.at(12, 16), (uint16_t)0xFFFF);  // the stem, bottom row
    CHECK_EQ(fb.at(10, 13), (uint16_t)0);       // column 0 middle row empty for 'I'
    fb.fill(0);
    r = draw_text(fb, 0, 0, "AB", 2, 0xFFFF);
    CHECK_EQ(r.w, (int16_t)22); CHECK_EQ(r.h, (int16_t)14);  // 2 chars: 6*2*2 - 2
    CHECK_EQ(text_width("AB", 2), 22); CHECK_EQ(text_width("", 2), 0);
    CHECK_EQ(fb.at(0, 0), (uint16_t)0); CHECK_EQ(fb.at(0, 2), (uint16_t)0xFFFF);  // 'A' col0 = 0x7E: rows 1..6 set, scale 2 -> y 2..13
    fb.fill(0); draw_text(fb, 318, 238, "XYZ", 2, 0xFFFF);  // clipped, no crash
    CHECK_EQ(fb.at(319, 239), (uint16_t)0xFFFF);
}
TEST_MAIN()
