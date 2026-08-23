// Not a test: renders one PPM per screen state into ./frames/ so a human (or an agent with
// eyes) can look at the face before it ever reaches the panel. Run it from a build dir:
//   cmake --build build-tests --target render_frames && (cd build-tests && ./render_frames)
#include <sys/stat.h>
#include <cstdio>
#include <cstring>
#include "pip/face.hpp"
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
    return 0;
}
