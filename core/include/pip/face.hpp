#pragma once
#include <cstdint>
#include "pip/protocol.hpp"
namespace pip {
struct Rect { int16_t x, y, w, h; bool empty() const { return w <= 0 || h <= 0; } };
Rect rect_union(Rect a, Rect b);
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
// RGB565 landscape framebuffer, 320x240, 150 KB. Lives in .bss on the Pico 2 W (520 KB SRAM).
struct Framebuffer {
    static constexpr int W = 320, H = 240;
    uint16_t px[W * H];
    void fill(uint16_t c);
    void fill_rect(Rect r, uint16_t c);                       // clipped
    void fill_ellipse(int cx, int cy, int rx, int ry, uint16_t c);   // filled, clipped
    uint16_t at(int x, int y) const { return px[y * W + x]; }
};
struct EyeShape {
    int16_t rx, ry;             // half-size
    int16_t lid_top_pct;        // 0 open .. 100 shut, from the top
    int16_t lid_bottom_pct;     // 0 .. 100, from the bottom (happy squint)
    int16_t pupil_dx, pupil_dy; // pupil offset from the eye centre
    int16_t pupil_r;
};
struct FaceShape { EyeShape left, right; };
FaceShape shape_for(Emotion e);
// Eyes-first face. Call tick() every frame; it animates toward the target emotion, blinks
// on its own, redraws only what changed, and returns the dirty rectangle to push.
class Face {
public:
    static constexpr int LEFT_CX = 110, RIGHT_CX = 210, EYE_CY = 120;
    static constexpr uint16_t BG = rgb565(12, 12, 28), EYE = rgb565(240, 240, 255), PUPIL = rgb565(20, 20, 40);
    Face();
    void set_emotion(Emotion e);
    Emotion emotion() const { return target_emotion_; }
    Rect tick(uint32_t dt_ms, Framebuffer& fb);
    const FaceShape& current() const { return cur_; }
private:
    static void step(int16_t& v, int16_t target, uint32_t dt_ms);
    static Rect eye_bounds(int cx, const EyeShape& e);
    static void draw_eye(Framebuffer& fb, int cx, const EyeShape& e);
    Emotion target_emotion_ = Emotion::Idle;
    FaceShape cur_, target_, drawn_;
    bool first_ = true, blinking_ = false;
    uint32_t t_ms_ = 0, next_blink_ms_ = 2800, blink_until_ms_ = 0;
    unsigned blink_n_ = 0;
};
}
