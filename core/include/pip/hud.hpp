#pragma once
#include <cstddef>
#include <cstdint>
#include "pip/fb.hpp"
#include "pip/protocol.hpp"
namespace pip {
// What the strip is showing. The brain owns the top half of these through /hud; the body
// owns the sensor and link half.
struct HudState {
    float lux = -1; bool night = false; float temp_c = 0;
    long reflex_us = -1, judge_ms = -1;                 // -1 = never measured
    bool wire = false, wifi = false, brain = false, cortex = false;
    char mind = '-';
    char scene[16] = {0};
};
// The bottom 40 rows: what every part of Pip is doing, in a strip a camera can read.
// It repaints only when something that is actually on screen changed, so a lux reading
// drifting by a hundredth costs nothing.
class Hud {
public:
    static constexpr int Y0 = 200, H = 40;
    static constexpr int BAR_X = 6, BAR_Y = 222, BAR_W = 50, BAR_H = 10, MOON_CX = 62, MOON_CY = 227, TEXT_X = 76, TEXT_Y = 224;
    static constexpr int CAPTION_X = 6, CAPTION_Y = 203, GLYPH_Y = 203, GLYPH_RIGHT = 6;
    static constexpr uint16_t BG = rgb565(6, 6, 12), FG = rgb565(200, 200, 215), DIM = rgb565(60, 60, 80),
                              OK = rgb565(60, 220, 120), WARN = rgb565(240, 180, 40);
    void apply(const HudUpdate& u);                     // merges the has_* fields
    void set_senses(float lux, bool night, float temp_c, bool wire, bool wifi);
    const HudState& state() const { return s_; }
    Rect draw(Framebuffer& fb, bool force);             // the whole strip when anything changed, else an empty Rect
    static void fmt_us(long us, char* out, size_t cap); // "95us", "1.2ms", "--"
    static void fmt_ms(long ms, char* out, size_t cap); // "5.8s", "850ms", "--"
private:
    // What is actually painted, in a form with no padding, so memcmp is a fair question.
    // Comparing this instead of HudState is what makes a sub-pixel sensor wobble free.
    struct Render {
        char scene[16];
        char us[12], ms[12], temp[8];
        uint8_t bar_px;
        uint8_t flags;      // bit0 wire, 1 wifi, 2 brain, 3 cortex, 4 night
        char mind;
        char pad;           // keeps the struct an even size; always zero
    };
    Render make_render() const;
    HudState s_;
    Render drawn_{};
    bool first_ = true;
};
}
