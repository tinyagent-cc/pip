#pragma once
#include <cstdint>
#include "pip/fb.hpp"
#include "pip/protocol.hpp"
namespace pip {
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
