// Not a test: renders one PPM per screen state into ./frames/ so a human (or an agent with
// eyes) can look at the face before it ever reaches the panel. Run it from a build dir:
//   cmake --build build-tests --target render_frames && (cd build-tests && ./render_frames)
//
//   ./render_frames                       every emotion, bubble, talking and HUD state (320x240)
//   ./render_frames --scenes              one 640x480 frame per demo scene, for docs/frames/
//   ./render_frames --scene night         just that one
//   ./render_frames --scenes --reflex-us 95 --judge-ms 9758
//                                         the HUD numbers to paint; -1 leaves the "--" that
//                                         means nobody has measured it yet
//
// PPM in, PNG out: scripts/frames-to-png.py turns the scene PPMs into the committed PNGs.
#include <sys/stat.h>
#include <cstdio>
#include <cstdlib>
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
// 2x nearest neighbour: the panel is 320x240 and a 640x480 PNG is what a reader can
// actually see in a README. Nearest neighbour keeps the 5x7 font's pixels square instead
// of smearing them.
static void dump2x(const char* name) {
    char path[128];
    std::snprintf(path, sizeof path, "frames/%s.ppm", name);
    FILE* f = std::fopen(path, "wb");
    if (!f) { std::fprintf(stderr, "cannot write %s\n", path); return; }
    std::fprintf(f, "P6\n%d %d\n255\n", Framebuffer::W * 2, Framebuffer::H * 2);
    for (int y = 0; y < Framebuffer::H; ++y) {
        for (int rep = 0; rep < 2; ++rep) {
            for (int x = 0; x < Framebuffer::W; ++x) {
                uint16_t p = fb.px[y * Framebuffer::W + x];
                unsigned char rgb[3] = {(unsigned char)((p >> 8) & 0xF8), (unsigned char)((p >> 3) & 0xFC), (unsigned char)((p << 3) & 0xF8)};
                std::fwrite(rgb, 1, 3, f);
                std::fwrite(rgb, 1, 3, f);
            }
        }
    }
    std::fclose(f);
    std::printf("%s\n", path);
}
static void settle(Face& f, int frames = 60) { for (int i = 0; i < frames; ++i) f.tick(16, fb); }

// One representative frame per demo scene: the whole screen, face and HUD, the way the
// camera sees it. Every number here is a measurement or the "--" that says there isn't one.
struct SceneArgs { long reflex_us = -1, judge_ms = -1; };

static void render_scene(const char* name, const SceneArgs& a) {
    Face f;
    Hud h;
    HudUpdate u;
    u.has_scene = true;
    std::snprintf(u.scene, sizeof u.scene, "%s", name);
    u.has_brain = true; u.brain = true;
    u.has_cortex = true; u.cortex = true;
    u.has_mind = true; u.mind = '-';
    u.has_reflex_us = true; u.reflex_us = a.reflex_us;   // -1 until the bench measures the wire
    float lux = 140.0f, temp = 22.0f;
    bool night = false, cortex = true;
    const char* bubble = nullptr;
    Emotion e = Emotion::Idle;
    bool talking = false;

    if (!std::strcmp(name, "reflex")) {
        e = Emotion::Wink;
    } else if (!std::strcmp(name, "judge")) {
        e = Emotion::Thinking;
        bubble = "Let me think...";
        u.has_judge_ms = true; u.judge_ms = a.judge_ms;
        u.mind = 'J';
    } else if (!std::strcmp(name, "night")) {
        e = Emotion::Sleepy;
        night = true; lux = 3.0f;
        bubble = "Rule capped the LED to 40.";
    } else if (!std::strcmp(name, "fallback")) {
        e = Emotion::Happy;
        cortex = false;
        u.cortex = false;
        u.mind = '5';
    } else if (!std::strcmp(name, "fever")) {
        e = Emotion::Alert;
        temp = 36.0f;
    } else if (!std::strcmp(name, "who")) {
        e = Emotion::Listening;
        bubble = "The desk in the room has a clock on the wall.";
    } else if (!std::strcmp(name, "tour")) {
        e = Emotion::Talking;
        bubble = "I'm Pip.";
        talking = true;
    } else if (Hud::caption_style(name).icon != Hud::ICON_NONE) {
        // A machinery-ticker caption ("deep-agent", "tool search", "rete
        // press-wink", "ears whisper"): the face thinks while the caption
        // names the tiny_agent part at work.
        e = Emotion::Thinking;
        u.has_judge_ms = true; u.judge_ms = a.judge_ms;
        u.mind = 'J';
    } else {
        std::fprintf(stderr, "unknown scene %s\n", name);
        return;
    }
    (void)cortex;

    f.tick(16, fb);
    f.set_night(night);
    f.set_emotion(talking ? Emotion::Happy : e);
    settle(f);
    if (talking) { f.set_talking(true); settle(f, 2); }
    if (bubble) { f.say(bubble, 5000); settle(f, 4); }
    h.set_senses(lux, night, temp, true, true);
    h.apply(u);
    h.draw(fb, true);
    // Ticker captions carry spaces; the file on disk should not.
    char fname[32];
    std::snprintf(fname, sizeof fname, "%s", name);
    for (char* c = fname; *c; ++c) if (*c == ' ') *c = '-';
    dump2x(fname);
}

static const char* kScenes[] = {"reflex", "judge", "night", "fallback", "fever", "who", "tour"};

int main(int argc, char** argv) {
    mkdir("frames", 0755);
    SceneArgs args;
    const char* one = nullptr;
    bool all_scenes = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--scenes")) all_scenes = true;
        else if (!std::strcmp(argv[i], "--scene") && i + 1 < argc) one = argv[++i];
        else if (!std::strcmp(argv[i], "--reflex-us") && i + 1 < argc) args.reflex_us = std::atol(argv[++i]);
        else if (!std::strcmp(argv[i], "--judge-ms") && i + 1 < argc) args.judge_ms = std::atol(argv[++i]);
        else { std::fprintf(stderr, "unknown argument %s\n", argv[i]); return 2; }
    }
    if (one || all_scenes) {
        if (one) render_scene(one, args);
        if (all_scenes) for (const char* s : kScenes) render_scene(s, args);
        return 0;
    }
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
