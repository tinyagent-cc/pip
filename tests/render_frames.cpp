// Not a test: renders one PPM per screen state into ./frames/ so a human (or an agent with
// eyes) can look at the face before it ever reaches the panel. Run it from a build dir:
//   cmake --build build-tests --target render_frames && (cd build-tests && ./render_frames)
#include <sys/stat.h>
#include <cstdio>
#include <cstring>
#include "pip/face.hpp"
#include "pip/hud.hpp"
using namespace pip;

static Framebuffer fb;

static void dump(const char* name) {
    char path[128];
    std::snprintf(path, sizeof path, "frames/%s.ppm", name);
    FILE* f = std::fopen(path, "wb");
    if (!f) { std::fprintf(stderr, "cannot write %s\n", path); return; }
    std::fprintf(f, "P6\n%d %d\n255\n", Framebuffer::W, Framebuffer::H);
    for (int i = 0; i < Framebuffer::W * Framebuffer::H; ++i) {
        uint16_t p = fb.px[i];
        unsigned char rgb[3] = {(unsigned char)((p >> 8) & 0xF8), (unsigned char)((p >> 3) & 0xFC), (unsigned char)((p << 3) & 0xF8)};
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
    std::printf("%s\n", path);
}
static void settle(Face& f, int frames = 60) { for (int i = 0; i < frames; ++i) f.tick(16, fb); }

int main() {
    mkdir("frames", 0755);
    // Everything outside the face region stays a flat marker colour, so a frame that leaks
    // into the HUD strip is obvious at a glance.
    fb.fill(rgb565(60, 0, 60));
    for (int e = 0; e < (int)Emotion::Count; ++e) {
        Face f;
        f.tick(16, fb);
        f.set_emotion((Emotion)e);
        settle(f);
        char name[32];
        std::snprintf(name, sizeof name, "face-%s", emotion_name((Emotion)e));
        dump(name);
    }
    {
        Face f; f.tick(16, fb); f.set_emotion(Emotion::Happy); settle(f);
        f.say("hello, i am pip and this is my speech bubble", 5000);
        settle(f, 4);
        dump("bubble-two-lines");
        f.say("short one", 5000);
        settle(f, 4);
        dump("bubble-one-line");
    }
    {
        Face f; f.tick(16, fb); f.set_emotion(Emotion::Happy); settle(f);
        f.set_talking(true);
        settle(f, 2); dump("talking-open");
        settle(f, 9); dump("talking-shut");
    }
    {
        Face f; f.tick(16, fb); f.set_emotion(Emotion::Listening); settle(f);
        settle(f, 6); dump("listening-glyph");
    }
    // Full screen: face plus HUD, the thing the camera actually sees.
    {
        Face f; f.tick(16, fb); f.set_emotion(Emotion::Thinking); settle(f);
        Hud h;
        h.set_senses(140.0f, false, 41.0f, true, true);
        HudUpdate u;
        u.has_scene = true; std::snprintf(u.scene, sizeof u.scene, "reflex");
        u.has_reflex_us = true; u.reflex_us = 95;
        u.has_judge_ms = true; u.judge_ms = 5800;
        u.has_brain = true; u.brain = true;
        u.has_cortex = true; u.cortex = true;
        u.has_mind = true; u.mind = 'J';
        h.apply(u);
        h.draw(fb, true);
        dump("screen-reflex");
        Hud h2;
        h2.set_senses(3.0f, true, 22.0f, false, true);
        HudUpdate u2;
        u2.has_scene = true; std::snprintf(u2.scene, sizeof u2.scene, "night");
        u2.has_mind = true; u2.mind = '5';
        h2.apply(u2);
        Face g; g.tick(16, fb); g.set_night(true); settle(g);
        g.say("that is much too bright", 5000);
        settle(g, 4);
        h2.draw(fb, true);
        dump("screen-night");
    }
    return 0;
}
