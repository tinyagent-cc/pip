#include <cstring>
#include "check.h"
#include "pip/hud.hpp"
using namespace pip;
static Framebuffer fb;
static void run() {
    char b[16];
    Hud::fmt_us(95, b, sizeof b); CHECK_STREQ(b, "95us");
    Hud::fmt_us(0, b, sizeof b); CHECK_STREQ(b, "0us");
    Hud::fmt_us(999, b, sizeof b); CHECK_STREQ(b, "999us");
    Hud::fmt_us(1234, b, sizeof b); CHECK_STREQ(b, "1.2ms");
    Hud::fmt_us(99999, b, sizeof b); CHECK_STREQ(b, "99.9ms");
    Hud::fmt_us(614181, b, sizeof b); CHECK_STREQ(b, "614ms");
    Hud::fmt_us(1450000, b, sizeof b); CHECK_STREQ(b, "1.4s");
    Hud::fmt_us(-1, b, sizeof b); CHECK_STREQ(b, "--");
    Hud::fmt_ms(5800, b, sizeof b); CHECK_STREQ(b, "5.8s");
    Hud::fmt_ms(850, b, sizeof b); CHECK_STREQ(b, "850ms");
    Hud::fmt_ms(999, b, sizeof b); CHECK_STREQ(b, "999ms");
    Hud::fmt_ms(-1, b, sizeof b); CHECK_STREQ(b, "--");

    fb.fill(0x0BAD);
    Hud h;
    Rect r = h.draw(fb, false);
    CHECK_EQ(r.x, (int16_t)0); CHECK_EQ(r.y, (int16_t)Hud::Y0);
    CHECK_EQ(r.w, (int16_t)Framebuffer::W); CHECK_EQ(r.h, (int16_t)Hud::H);
    CHECK(h.draw(fb, false).empty());              // nothing changed, nothing to push
    CHECK(!h.draw(fb, true).empty());              // force repaints anyway

    // The face region above is never touched.
    for (int y = 0; y < Hud::Y0; ++y) CHECK_EQ(fb.at(0, y), (uint16_t)0x0BAD);
    CHECK_EQ(fb.at(300, 238), Hud::BG);

    // An empty scene reads as a dim "idle" caption, not as a blank strip.
    CHECK_EQ(fb.at(10, 203), Hud::DIM);            // 'i' of "idle", full-height column
    // Every link glyph starts down.
    CHECK_EQ(fb.at(256, 204), Hud::DIM);           // 'W' left column
    CHECK_EQ(h.state().mind, '-');
    CHECK_EQ(h.state().reflex_us, -1L);

    HudUpdate u;
    u.has_judge_ms = true; u.judge_ms = 5800;
    h.apply(u);
    CHECK_EQ(h.state().judge_ms, 5800L);
    CHECK(!h.draw(fb, false).empty());             // a changed value repaints the strip
    CHECK(h.draw(fb, false).empty());

    HudUpdate u2;
    u2.has_scene = true; std::snprintf(u2.scene, sizeof u2.scene, "reflex");
    u2.has_brain = true; u2.brain = true;
    u2.has_mind = true; u2.mind = 'J';
    h.apply(u2);
    CHECK_STREQ(h.state().scene, "reflex");
    CHECK(h.state().brain); CHECK_EQ(h.state().mind, 'J');
    CHECK_EQ(h.state().judge_ms, 5800L);           // apply merges, it does not replace
    h.draw(fb, false);
    CHECK_EQ(fb.at(6, 208), Hud::FG);              // 'r' of "reflex", stem
    CHECK_EQ(fb.at(280, 204), Hud::OK);            // 'B' brain, now up
    CHECK_EQ(fb.at(256, 204), Hud::DIM);           // 'W' wire, still down

    // Lux bar: full at 200 lux, empty at 0, and the moon only shows at night.
    h.set_senses(200.0f, false, 25.0f, true, true);
    h.draw(fb, false);
    CHECK_EQ(fb.at(44, 227), Hud::OK);
    CHECK_EQ(fb.at(256, 204), Hud::OK);            // 'W' wire, up now
    CHECK_EQ(fb.at(52, 227), Hud::BG);             // no moon by day
    h.set_senses(0.0f, true, 25.0f, true, true);
    h.draw(fb, false);
    CHECK_EQ(fb.at(44, 227), Hud::BG);             // bar empty
    CHECK_EQ(fb.at(7, 227), Hud::BG);
    CHECK_EQ(fb.at(52, 227), Hud::FG);             // moon
    h.set_senses(20.0f, true, 25.0f, true, true);
    h.draw(fb, false);
    CHECK_EQ(fb.at(7, 227), Hud::OK);              // a low reading still shows something
    // Log scale, not linear: 20 lux of 200 fills well past a tenth of the bar.
    CHECK_EQ(fb.at(26, 227), Hud::OK);
    CHECK_EQ(fb.at(44, 227), Hud::BG);

    // A change too small to move a pixel does not cost an SPI push.
    h.set_senses(20.0f, true, 25.02f, true, true);
    CHECK(h.draw(fb, false).empty());
}
TEST_MAIN()
