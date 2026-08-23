#include <cstdio>
#include <cstring>
#include "check.h"
#include "pip/face.hpp"
using namespace pip;
static Framebuffer fb;   // 150 KB, static on purpose
static void settle(Face& f, int frames = 40) { for (int i = 0; i < frames; ++i) f.tick(16, fb); }
static void run_ms(Face& f, int ms) { for (int t = 0; t < ms; t += 16) f.tick(16, fb); }

// The HUD owns rows 200..239. Paint them with a value the face never uses and prove no
// emotion, animation or bubble writes there.
static void hud_strip_is_sacred() {
    for (int e = 0; e < (int)Emotion::Count; ++e) {
        Face f;
        f.tick(16, fb);
        for (int y = Face::FACE_H; y < Framebuffer::H; ++y)
            for (int x = 0; x < Framebuffer::W; ++x) fb.px[y * Framebuffer::W + x] = 0x0BAD;
        f.set_emotion((Emotion)e);
        settle(f, 60);
        f.say("a long enough sentence to wrap onto two lines", 5000);
        settle(f, 20);
        f.set_talking(true);
        settle(f, 30);
        f.set_talking(false);
        settle(f, 20);
        bool clean = true;
        for (int y = Face::FACE_H; y < Framebuffer::H; ++y)
            for (int x = 0; x < Framebuffer::W; ++x) if (fb.at(x, y) != 0x0BAD) clean = false;
        CHECK(clean);
    }
}

static void run() {
    CHECK_EQ(rgb565(255, 255, 255), (uint16_t)0xFFFF); CHECK_EQ(rgb565(255, 0, 0), (uint16_t)0xF800);
    fb.fill(0); fb.fill_ellipse(100, 100, 20, 10, 0xFFFF);
    CHECK_EQ(fb.at(100, 100), (uint16_t)0xFFFF); CHECK_EQ(fb.at(119, 100), (uint16_t)0xFFFF);
    CHECK_EQ(fb.at(100, 109), (uint16_t)0xFFFF); CHECK_EQ(fb.at(121, 100), (uint16_t)0);
    CHECK_EQ(fb.at(118, 109), (uint16_t)0);   // outside the ellipse corner
    fb.fill_rect(Rect{-5, -5, 10, 10}, 0x1234); CHECK_EQ(fb.at(0, 0), (uint16_t)0x1234); CHECK_EQ(fb.at(5, 5), (uint16_t)0);  // clipped

    const uint16_t IDLE_BG = mood_bg(Emotion::Idle);
    Face face;
    Rect d = face.tick(16, fb);
    // The first frame paints the face region and only the face region.
    CHECK(!d.empty()); CHECK_EQ(d.x, (int16_t)0); CHECK_EQ(d.y, (int16_t)0);
    CHECK_EQ(d.w, (int16_t)Framebuffer::W); CHECK_EQ(d.h, (int16_t)Face::FACE_H);
    settle(face);
    CHECK(face.emotion() == Emotion::Idle);
    CHECK_EQ(fb.at(Face::LEFT_CX, Face::EYE_CY), Face::PUPIL);   // pupil centred at idle
    CHECK_EQ(fb.at(Face::LEFT_CX - 30, Face::EYE_CY), Face::EYE);
    CHECK_EQ(fb.at(10, 10), IDLE_BG);
    CHECK_EQ(fb.at(Face::MOUTH_CX, Face::MOUTH_CY), Face::LINE);   // the flat idle mouth
    // Brows sit above the eyes and do not touch them.
    CHECK_EQ(fb.at(Face::LEFT_CX, Face::EYE_CY + Face::BROW_DY), Face::LINE);
    CHECK_EQ(fb.at(Face::LEFT_CX, Face::EYE_CY - 50 - 4), IDLE_BG);

    face.set_emotion(Emotion::Wink); settle(face);
    CHECK(face.current().left.lid_top_pct == 100); CHECK(face.current().right.lid_top_pct == 0);
    CHECK_EQ(fb.at(Face::LEFT_CX, Face::EYE_CY), mood_bg(Emotion::Wink));   // left eye shut
    CHECK_EQ(fb.at(Face::RIGHT_CX, Face::EYE_CY), Face::PUPIL);             // right open
    // A fully shut eye must leave no stray pixel anywhere in its footprint, including the
    // single-pixel row at the very bottom of the arc.
    int shut_ry = face.current().left.ry;
    for (int y = Face::EYE_CY - shut_ry; y <= Face::EYE_CY + shut_ry; ++y)
        CHECK_EQ(fb.at(Face::LEFT_CX, y), mood_bg(Emotion::Wink));

    // Mood tint: a background change repaints the whole face region at once.
    face.set_emotion(Emotion::Happy);
    d = face.tick(16, fb);
    CHECK_EQ(d.h, (int16_t)Face::FACE_H);
    CHECK_EQ(fb.at(10, 10), mood_bg(Emotion::Happy));
    settle(face);
    CHECK(face.current().left.ry == shape_for(Emotion::Happy).left.ry);
    CHECK(face.current().left.lid_bottom_pct == shape_for(Emotion::Happy).left.lid_bottom_pct);
    // The bottom of a width-60 smile arc, 30 px under the mouth line.
    CHECK_EQ(fb.at(Face::MOUTH_CX, Face::MOUTH_CY + 28), Face::LINE);

    face.set_emotion(Emotion::Sad); settle(face, 80);
    CHECK(face.current().lbrow.angle_deg == shape_for(Emotion::Sad).lbrow.angle_deg);
    CHECK_EQ(fb.at(Face::MOUTH_CX, Face::MOUTH_CY - 23), Face::LINE);   // top of the frown arc

    for (int e = 0; e < (int)Emotion::Count; ++e) { face.set_emotion((Emotion)e); settle(face, 60); }

    // Return to idle.
    CHECK_EQ(Face::hold_ms_for(Emotion::Wink), 3000u);
    CHECK_EQ(Face::hold_ms_for(Emotion::Alert), 3000u);
    CHECK_EQ(Face::hold_ms_for(Emotion::Surprised), 3000u);
    CHECK_EQ(Face::hold_ms_for(Emotion::Happy), 10000u);
    CHECK_EQ(Face::hold_ms_for(Emotion::Sad), 10000u);
    CHECK_EQ(Face::hold_ms_for(Emotion::Idle), 0u);
    CHECK_EQ(Face::hold_ms_for(Emotion::Thinking), 0u);
    CHECK_EQ(Face::hold_ms_for(Emotion::Listening), 0u);
    {
        Face f; f.tick(16, fb);
        f.set_emotion(Emotion::Wink);
        run_ms(f, 2000); CHECK(f.emotion() == Emotion::Wink);
        run_ms(f, 1200); CHECK(f.emotion() == Emotion::Idle);
        f.set_emotion(Emotion::Thinking); run_ms(f, 6000);
        CHECK(f.emotion() == Emotion::Thinking);       // holds until the brain changes it
    }
    {
        Face f; f.tick(16, fb);
        f.set_night(true);
        CHECK(f.emotion() == Emotion::Sleepy);
        f.set_emotion(Emotion::Wink);
        run_ms(f, 3200);
        CHECK(f.emotion() == Emotion::Sleepy);          // night sends expiry to Sleepy, not Idle
        f.set_night(false);
        CHECK(f.emotion() == Emotion::Idle);
    }

    // Bubble.
    {
        Face f; f.tick(16, fb); settle(f);
        CHECK(!f.bubble_visible());
        f.say("hello world", 3000);
        CHECK(f.bubble_visible());
        settle(f, 2);
        CHECK_EQ(fb.at(290, 186), Face::BUBBLE_FILL);            // inside the box, below both lines
        CHECK_EQ(fb.at(160, 142), Face::BUBBLE_BORDER);          // top edge
        CHECK_EQ(fb.at(16, 148), Face::BUBBLE_TEXT);             // 'h' stem, line 1
        run_ms(f, 3100);
        CHECK(!f.bubble_visible());
        CHECK_EQ(fb.at(290, 186), IDLE_BG);                      // and the box is gone
        f.say("this sentence is definitely longer than a single line", 3000);
        settle(f, 2);
        CHECK(f.bubble_visible());
        CHECK_EQ(fb.at(24, 166), Face::BUBBLE_TEXT);             // 'd' upright, line 2
        f.clear_say();
        CHECK(!f.bubble_visible());
        f.say("", 3000);
        CHECK(!f.bubble_visible());                              // nothing to show, no empty box
    }

    // Talking: the mouth animates and the emotion underneath comes back.
    {
        Face f; f.tick(16, fb);
        f.set_emotion(Emotion::Happy); settle(f, 60);
        f.set_talking(true);
        CHECK(f.emotion() == Emotion::Talking);
        bool saw_line = false, saw_bg = false;
        for (int t = 0; t < 320; t += 16) {
            f.tick(16, fb);
            uint16_t c = fb.at(Face::MOUTH_CX, Face::MOUTH_CY + 10);
            if (c == Face::LINE) saw_line = true;
            else if (c == mood_bg(Emotion::Happy)) saw_bg = true;
        }
        CHECK(saw_line); CHECK(saw_bg);
        f.set_talking(false);
        CHECK(f.emotion() == Emotion::Happy);
        settle(f, 40);
        CHECK(f.current().mouth.kind == MouthKind::Smile);
    }

    // Saccade: idle pupils wander on their own within a few seconds.
    {
        Face f; f.tick(16, fb);
        bool moved = false;
        for (int i = 0; i < 150; ++i) { f.tick(16, fb); if (f.current().left.pupil_dx != 0) moved = true; }
        CHECK(moved);
    }

    // rect_union's two early returns: an empty operand contributes nothing, either side.
    Rect some{10, 20, 30, 40}, none{0, 0, 0, 0}, flat{5, 5, 12, 0};
    Rect u = rect_union(none, some);
    CHECK_EQ(u.x, some.x); CHECK_EQ(u.y, some.y); CHECK_EQ(u.w, some.w); CHECK_EQ(u.h, some.h);
    u = rect_union(some, flat);
    CHECK_EQ(u.x, some.x); CHECK_EQ(u.y, some.y); CHECK_EQ(u.w, some.w); CHECK_EQ(u.h, some.h);
    CHECK(rect_union(none, flat).empty());

    // Blink: at idle, advance past the first blink time and catch lids shut, then open again.
    {
        Face f2; f2.tick(16, fb); settle(f2);
        bool saw_shut = false, saw_open_after = false;
        for (int t = 0; t < 4000; t += 16) {
            f2.tick(16, fb);
            uint16_t c = fb.at(Face::LEFT_CX, Face::EYE_CY);
            if (c == IDLE_BG) saw_shut = true; else if (saw_shut && c == Face::PUPIL) saw_open_after = true;
        }
        CHECK(saw_shut); CHECK(saw_open_after);
    }

    hud_strip_is_sacred();
}
TEST_MAIN()
