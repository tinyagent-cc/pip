#include <cstdio>
#include <cstring>
#include "check.h"
#include "pip/face.hpp"
using namespace pip;
static Framebuffer fb;   // 150 KB, static on purpose
static void dump_ppm(const char* path) {
    FILE* f = std::fopen(path, "wb"); if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", Framebuffer::W, Framebuffer::H);
    for (int i = 0; i < Framebuffer::W * Framebuffer::H; ++i) {
        uint16_t p = fb.px[i];
        unsigned char rgb[3] = {(unsigned char)((p >> 8) & 0xF8), (unsigned char)((p >> 3) & 0xFC), (unsigned char)((p << 3) & 0xF8)};
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
}
static void settle(Face& f, int frames = 40) { for (int i = 0; i < frames; ++i) f.tick(16, fb); }
static void run() {
    CHECK_EQ(rgb565(255, 255, 255), (uint16_t)0xFFFF); CHECK_EQ(rgb565(255, 0, 0), (uint16_t)0xF800);
    fb.fill(0); fb.fill_ellipse(100, 100, 20, 10, 0xFFFF);
    CHECK_EQ(fb.at(100, 100), (uint16_t)0xFFFF); CHECK_EQ(fb.at(119, 100), (uint16_t)0xFFFF);
    CHECK_EQ(fb.at(100, 109), (uint16_t)0xFFFF); CHECK_EQ(fb.at(121, 100), (uint16_t)0);
    CHECK_EQ(fb.at(118, 109), (uint16_t)0);   // outside the ellipse corner
    fb.fill_rect(Rect{-5, -5, 10, 10}, 0x1234); CHECK_EQ(fb.at(0, 0), (uint16_t)0x1234); CHECK_EQ(fb.at(5, 5), (uint16_t)0);  // clipped

    Face face;
    Rect d = face.tick(16, fb);
    CHECK(!d.empty()); CHECK_EQ(d.x, (int16_t)0); CHECK_EQ(d.w, (int16_t)Framebuffer::W);   // first frame paints everything
    settle(face);
    CHECK(face.emotion() == Emotion::Idle);
    CHECK_EQ(fb.at(Face::LEFT_CX, Face::EYE_CY), Face::PUPIL);   // pupil centred at idle
    CHECK_EQ(fb.at(Face::LEFT_CX - 30, Face::EYE_CY), Face::EYE);
    CHECK_EQ(fb.at(10, 10), Face::BG);
    d = face.tick(16, fb); CHECK(d.empty());   // settled, no blink yet (first blink after 2.8 s)
    dump_ppm("face_idle.ppm");

    face.set_emotion(Emotion::Wink); settle(face);
    CHECK(face.current().left.lid_top_pct == 100); CHECK(face.current().right.lid_top_pct == 0);
    CHECK_EQ(fb.at(Face::LEFT_CX, Face::EYE_CY), Face::BG);      // left eye shut
    CHECK_EQ(fb.at(Face::RIGHT_CX, Face::EYE_CY), Face::PUPIL);  // right open
    // A fully shut eye (lid_top_pct 100) must leave no stray pixel anywhere in its
    // ellipse footprint, including the single-pixel row at the very bottom of the arc.
    int shut_ry = face.current().left.ry;
    for (int y = Face::EYE_CY - shut_ry; y <= Face::EYE_CY + shut_ry; ++y)
        CHECK_EQ(fb.at(Face::LEFT_CX, y), Face::BG);
    dump_ppm("face_wink.ppm");

    face.set_emotion(Emotion::Happy); d = face.tick(16, fb); CHECK(!d.empty());   // animating
    settle(face);
    CHECK(face.current().left.ry == shape_for(Emotion::Happy).left.ry);
    CHECK(face.current().left.lid_bottom_pct == shape_for(Emotion::Happy).left.lid_bottom_pct);
    dump_ppm("face_happy.ppm");

    face.set_emotion(Emotion::Sleepy); settle(face);
    CHECK(face.current().left.lid_top_pct == shape_for(Emotion::Sleepy).left.lid_top_pct);
    CHECK(face.current().right.ry == shape_for(Emotion::Sleepy).right.ry);
    dump_ppm("face_sleepy.ppm");
    face.set_emotion(Emotion::Thinking); settle(face);
    CHECK(face.current().left.pupil_dx == shape_for(Emotion::Thinking).left.pupil_dx);
    dump_ppm("face_thinking.ppm");
    face.set_emotion(Emotion::Alert); settle(face);
    CHECK(face.current().left.ry == shape_for(Emotion::Alert).left.ry);
    CHECK(face.current().right.pupil_r == shape_for(Emotion::Alert).right.pupil_r);
    dump_ppm("face_alert.ppm");

    // rect_union's two early returns: an empty operand contributes nothing, either side.
    Rect some{10, 20, 30, 40}, none{0, 0, 0, 0}, flat{5, 5, 12, 0};
    Rect u = rect_union(none, some);
    CHECK_EQ(u.x, some.x); CHECK_EQ(u.y, some.y); CHECK_EQ(u.w, some.w); CHECK_EQ(u.h, some.h);
    u = rect_union(some, flat);
    CHECK_EQ(u.x, some.x); CHECK_EQ(u.y, some.y); CHECK_EQ(u.w, some.w); CHECK_EQ(u.h, some.h);
    CHECK(rect_union(none, flat).empty());

    // Blink: at idle, advance past the first blink time and catch lids shut, then open again.
    Face f2; f2.tick(16, fb); settle(f2);
    bool saw_shut = false, saw_open_after = false;
    for (int t = 0; t < 4000; t += 16) {
        f2.tick(16, fb);
        uint16_t c = fb.at(Face::LEFT_CX, Face::EYE_CY);
        if (c == Face::BG) saw_shut = true; else if (saw_shut) saw_open_after = true;
    }
    CHECK(saw_shut); CHECK(saw_open_after);
}
TEST_MAIN()
