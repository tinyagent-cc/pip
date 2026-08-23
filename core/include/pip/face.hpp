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
struct BrowShape { int16_t angle_deg; int16_t raise; };   // angle: negative = inner end down (angry/sad); raise: px above the default line
enum class MouthKind : uint8_t { Flat, Smile, Frown, O, Open };
struct MouthShape { MouthKind kind; int16_t width; int16_t open_pct; };   // open_pct drives Open (talking), 0..100
struct FaceShape { EyeShape left, right; BrowShape lbrow, rbrow; MouthShape mouth; uint16_t bg; };
FaceShape shape_for(Emotion e);
uint16_t mood_bg(Emotion e);   // Talking has no colour of its own; it keeps the emotion it interrupted
// Compared field by field rather than with memcmp: these structs have padding, and a
// padding byte deciding whether the screen gets repainted is not a bug worth having.
inline bool operator==(const EyeShape& a, const EyeShape& b) {
    return a.rx == b.rx && a.ry == b.ry && a.lid_top_pct == b.lid_top_pct && a.lid_bottom_pct == b.lid_bottom_pct
        && a.pupil_dx == b.pupil_dx && a.pupil_dy == b.pupil_dy && a.pupil_r == b.pupil_r;
}
inline bool operator==(const BrowShape& a, const BrowShape& b) { return a.angle_deg == b.angle_deg && a.raise == b.raise; }
inline bool operator==(const MouthShape& a, const MouthShape& b) { return a.kind == b.kind && a.width == b.width && a.open_pct == b.open_pct; }

// The whole face. Call tick() every frame; it animates toward the target emotion, blinks
// and saccades on its own, runs the return-to-idle and bubble timers, redraws only what
// changed and returns the dirty rectangle to push. It never writes at or below FACE_H:
// that strip belongs to the HUD.
class Face {
public:
    static constexpr int FACE_H = 200;
    static constexpr int LEFT_CX = 110, RIGHT_CX = 210, EYE_CY = 88, MOUTH_CX = 160, MOUTH_CY = 165, BROW_DY = -62;
    static constexpr int BROW_HALF = 34, BROW_THICK = 4, MOUTH_THICK = 4;
    static constexpr uint16_t EYE = rgb565(240, 240, 255), PUPIL = rgb565(20, 20, 40), LINE = rgb565(230, 230, 240);
    static constexpr uint16_t BUBBLE_FILL = rgb565(245, 245, 250), BUBBLE_BORDER = rgb565(90, 90, 120), BUBBLE_TEXT = rgb565(20, 20, 40);
    static constexpr int BUBBLE_COLS = 24;
    Face();
    void set_emotion(Emotion e);                 // starts the hold timer for the timed emotions
    void set_night(bool n);                      // expiry lands on Sleepy instead of Idle while night
    void set_talking(bool on);                   // overlays Talking and restores the interrupted emotion when off
    void set_listening_level(uint8_t pct);       // waveform amplitude 0..100; without it the glyph animates on its own
    void say(const char* text, uint32_t hold_ms);
    void clear_say();
    bool bubble_visible() const { return bubble_; }
    bool night() const { return night_; }
    Emotion emotion() const { return talking_ ? Emotion::Talking : target_emotion_; }
    Rect tick(uint32_t dt_ms, Framebuffer& fb);
    const FaceShape& current() const { return cur_; }
    static uint32_t hold_ms_for(Emotion e);      // wink/alert/surprised 3000; happy/sad 10000; others 0 = until changed
    static Rect bubble_rect() { return Rect{8, 142, 304, 52}; }
private:
    static void step(int16_t& v, int16_t target, uint32_t dt_ms);
    static Rect eye_bounds(int cx, const EyeShape& e);
    static Rect brow_bounds(int cx, const BrowShape& b);
    static Rect mouth_bounds();
    static Rect glyph_bounds();
    static void brow_ends(int cx, bool left_eye, const BrowShape& b, int& x0, int& y0, int& x1, int& y1);
    static void draw_eye(Framebuffer& fb, int cx, const EyeShape& e, uint16_t bg);
    static void draw_brow(Framebuffer& fb, int cx, bool left_eye, const BrowShape& b);
    static void draw_mouth(Framebuffer& fb, const MouthShape& m, uint16_t bg);
    void draw_glyph(Framebuffer& fb, const int16_t h[3]) const;
    void draw_bubble(Framebuffer& fb) const;
    void draw_all(Framebuffer& fb, const FaceShape& show, const int16_t glyph[3], bool want_glyph) const;
    void apply_emotion(Emotion e);               // shape + timers, without disturbing the talking overlay
    void retarget();                             // base shape plus the current saccade offset
    void glyph_heights(int16_t out[3]) const;

    Emotion target_emotion_ = Emotion::Idle, prev_emotion_ = Emotion::Idle;
    FaceShape base_{}, cur_{}, target_{}, drawn_{};
    bool first_ = true, blinking_ = false, night_ = false, talking_ = false, bubble_ = false;
    int16_t listen_level_ = -1;                  // -1 = animate on its own
    int16_t sacc_dx_ = 0, sacc_dy_ = 0;
    int16_t drawn_glyph_[3] = {0, 0, 0};
    bool drawn_bubble_ = false;
    uint32_t drawn_bubble_id_ = 0, bubble_id_ = 0;
    uint32_t t_ms_ = 0, next_blink_ms_ = 2800, blink_until_ms_ = 0;
    uint32_t next_sacc_ms_ = 2000, sacc_until_ms_ = 0;
    uint32_t hold_until_ms_ = 0, bubble_until_ms_ = 0, talk_ms_ = 0;
    unsigned blink_n_ = 0, sacc_n_ = 0;
    char line1_[BUBBLE_COLS + 1] = {0}, line2_[BUBBLE_COLS + 1] = {0};
};
}
