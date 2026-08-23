#include "check.h"
#include "pip/draw.hpp"
using namespace pip;
static Framebuffer fb;
static void run() {
    fb.fill(0);
    Rect r = draw_line(fb, 10, 10, 50, 10, 0xFFFF, 1);
    CHECK_EQ(fb.at(30, 10), (uint16_t)0xFFFF); CHECK_EQ(fb.at(30, 11), (uint16_t)0);
    CHECK_EQ(r.y, (int16_t)10); CHECK_EQ(r.h, (int16_t)1);
    fb.fill(0); draw_line(fb, 10, 10, 50, 10, 0xFFFF, 5);
    CHECK_EQ(fb.at(30, 8), (uint16_t)0xFFFF); CHECK_EQ(fb.at(30, 12), (uint16_t)0xFFFF); CHECK_EQ(fb.at(30, 14), (uint16_t)0);
    fb.fill(0); draw_line(fb, 0, 0, 100, 100, 0xFFFF, 1); CHECK_EQ(fb.at(50, 50), (uint16_t)0xFFFF);
    fb.fill(0); draw_disc(fb, 100, 100, 10, 0xFFFF); CHECK_EQ(fb.at(100, 109), (uint16_t)0xFFFF); CHECK_EQ(fb.at(100, 111), (uint16_t)0);
    fb.fill(0); r = draw_arc(fb, 100, 100, 30, 0, 180, 0xFFFF, 3);   // lower half (y down): smile
    CHECK_EQ(fb.at(100, 130), (uint16_t)0xFFFF); CHECK_EQ(fb.at(100, 70), (uint16_t)0);
    CHECK(r.y >= 95 && r.y + r.h <= 135);
    fb.fill(0); draw_arc(fb, 100, 100, 30, 180, 360, 0xFFFF, 3);    // upper half: frown
    CHECK_EQ(fb.at(100, 70), (uint16_t)0xFFFF); CHECK_EQ(fb.at(100, 130), (uint16_t)0);
    fb.fill(0); r = draw_round_rect(fb, Rect{10, 10, 100, 40}, 8, 0x1111, 0xFFFF);
    CHECK_EQ(fb.at(60, 30), (uint16_t)0x1111); CHECK_EQ(fb.at(60, 10), (uint16_t)0xFFFF); CHECK_EQ(fb.at(10, 10), (uint16_t)0);  // corner cut
    CHECK_EQ(r.w, (int16_t)100);
    draw_round_rect(fb, Rect{300, 220, 100, 100}, 8, 0x1111, 0xFFFF);  // clipped, no crash
}
TEST_MAIN()
