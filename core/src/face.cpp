#include "pip/face.hpp"
#include <algorithm>
#include <cstring>
#include "pip/draw.hpp"
namespace pip {
namespace {
Rect clamp_face(Rect r) {
    if (r.empty()) return Rect{0, 0, 0, 0};
    int x0 = std::max(0, (int)r.x), y0 = std::max(0, (int)r.y);
    int x1 = std::min(Framebuffer::W, (int)r.x + (int)r.w), y1 = std::min(Face::FACE_H, (int)r.y + (int)r.h);
    if (x1 <= x0 || y1 <= y0) return Rect{0, 0, 0, 0};
    return Rect{(int16_t)x0, (int16_t)y0, (int16_t)(x1 - x0), (int16_t)(y1 - y0)};
}
}

uint16_t mood_bg(Emotion e) {
    switch (e) {
        case Emotion::Happy: return rgb565(40, 24, 8);
        case Emotion::Sleepy: return rgb565(4, 4, 8);
        case Emotion::Thinking: return rgb565(24, 10, 40);
        case Emotion::Alert: return rgb565(48, 28, 0);
        case Emotion::Surprised: return rgb565(30, 30, 40);
        case Emotion::Sad: return rgb565(14, 18, 30);
        case Emotion::Listening: return rgb565(8, 24, 32);
        case Emotion::Idle: case Emotion::Wink: case Emotion::Talking: default: return rgb565(12, 12, 28);
    }
}
FaceShape shape_for(Emotion e) {
    EyeShape base{40, 50, 0, 0, 0, 0, 14};
    FaceShape s{base, base, BrowShape{0, 0}, BrowShape{0, 0}, MouthShape{MouthKind::Flat, 40, 0}, mood_bg(e)};
    switch (e) {
        case Emotion::Idle: break;
        case Emotion::Happy:
            s.left.ry = s.right.ry = 36; s.left.lid_bottom_pct = s.right.lid_bottom_pct = 45;
            s.lbrow.raise = s.rbrow.raise = 4;
            s.mouth = MouthShape{MouthKind::Smile, 60, 0};
            break;
        case Emotion::Sleepy:
            s.left.lid_top_pct = s.right.lid_top_pct = 60; s.left.pupil_dy = s.right.pupil_dy = 8;
            s.lbrow.raise = s.rbrow.raise = -2;
            s.mouth = MouthShape{MouthKind::Flat, 30, 0};
            break;
        case Emotion::Thinking:
            s.left.pupil_dx = s.right.pupil_dx = 14; s.left.pupil_dy = s.right.pupil_dy = -16;
            s.left.lid_top_pct = s.right.lid_top_pct = 15;
            s.lbrow = BrowShape{15, 6};
            s.mouth = MouthShape{MouthKind::Flat, 30, 0};
            break;
        case Emotion::Alert:
            s.left.rx = s.right.rx = 44; s.left.ry = s.right.ry = 58; s.left.pupil_r = s.right.pupil_r = 10;
            s.lbrow.raise = s.rbrow.raise = 10;
            s.mouth = MouthShape{MouthKind::O, 24, 0};
            break;
        case Emotion::Wink:
            s.left.lid_top_pct = 100;
            s.mouth = MouthShape{MouthKind::Smile, 50, 0};
            break;
        case Emotion::Surprised:
            s.left.rx = s.right.rx = 46; s.left.ry = s.right.ry = 60; s.left.pupil_r = s.right.pupil_r = 9;
            s.lbrow.raise = s.rbrow.raise = 14;
            s.mouth = MouthShape{MouthKind::O, 24, 0};
            break;
        case Emotion::Sad:
            s.left.lid_top_pct = s.right.lid_top_pct = 35; s.left.pupil_dy = s.right.pupil_dy = 6;
            // The angle alone drops the inner ends onto the eyelids; the raise buys the gap
            // back so the face reads as sad instead of as a smudge.
            s.lbrow = BrowShape{-18, 6}; s.rbrow = BrowShape{-18, 6};
            s.mouth = MouthShape{MouthKind::Frown, 50, 0};
            break;
        case Emotion::Listening:
            s.left.ry = s.right.ry = 46;
            s.lbrow.raise = s.rbrow.raise = 6;
            s.mouth = MouthShape{MouthKind::Flat, 30, 0};
            break;
        case Emotion::Talking:
            // A standalone Talking shape exists only so the enum is total; Face::set_talking
            // builds the real one from the emotion it interrupted.
            s.mouth = MouthShape{MouthKind::Open, 30, 100};
            break;
        default: break;
    }
    return s;
}
uint32_t Face::hold_ms_for(Emotion e) {
    switch (e) {
        case Emotion::Wink: case Emotion::Alert: case Emotion::Surprised: return 3000;
        case Emotion::Happy: case Emotion::Sad: return 10000;
        default: return 0;
    }
}

Face::Face() {
    base_ = shape_for(Emotion::Idle);
    cur_ = target_ = drawn_ = base_;
}
void Face::retarget() {
    target_ = base_;
    target_.left.pupil_dx = (int16_t)(target_.left.pupil_dx + sacc_dx_);
    target_.right.pupil_dx = (int16_t)(target_.right.pupil_dx + sacc_dx_);
    target_.left.pupil_dy = (int16_t)(target_.left.pupil_dy + sacc_dy_);
    target_.right.pupil_dy = (int16_t)(target_.right.pupil_dy + sacc_dy_);
}
void Face::apply_emotion(Emotion e) {
    target_emotion_ = e;
    base_ = shape_for(e);
    uint32_t hold = hold_ms_for(e);
    hold_until_ms_ = hold ? t_ms_ + hold : 0;
    retarget();
}
void Face::set_emotion(Emotion e) {
    // A command arriving mid-speech decides what Pip goes back to but does not stop the
    // mouth: the audio ending is what ends Talking, not the brain changing its mind.
    if (talking_) {
        prev_emotion_ = e;
        target_emotion_ = e;
        hold_until_ms_ = 0;
        base_ = shape_for(e);
        base_.mouth = MouthShape{MouthKind::Open, 30, 100};
        retarget();
        return;
    }
    apply_emotion(e);
}
void Face::set_night(bool n) {
    if (night_ == n) return;
    night_ = n;
    if (talking_) return;
    if (n && target_emotion_ == Emotion::Idle) apply_emotion(Emotion::Sleepy);
    else if (!n && target_emotion_ == Emotion::Sleepy) apply_emotion(Emotion::Idle);
}
void Face::set_talking(bool on) {
    if (on == talking_) return;
    if (on) {
        prev_emotion_ = target_emotion_;
        talking_ = true;
        talk_ms_ = 0;
        hold_until_ms_ = 0;               // the hold restarts with the emotion Talking interrupted
        base_ = shape_for(prev_emotion_);
        base_.mouth = MouthShape{MouthKind::Open, 30, 100};
        retarget();
    } else {
        talking_ = false;
        apply_emotion(prev_emotion_);
    }
}
void Face::set_listening_level(uint8_t pct) { listen_level_ = (int16_t)(pct > 100 ? 100 : pct); }

void Face::say(const char* text, uint32_t hold_ms) {
    // Greedy wrap into two lines; a word longer than a line is broken rather than dropped,
    // and anything past two lines does not fit on a 320 px screen at scale 2.
    line1_[0] = line2_[0] = '\0';
    char* lines[2] = {line1_, line2_};
    size_t i = 0, n = text ? std::strlen(text) : 0;
    for (int l = 0; l < 2 && i < n; ++l) {
        while (i < n && text[i] == ' ') ++i;
        if (i >= n) break;
        size_t take = (n - i) < (size_t)BUBBLE_COLS ? (n - i) : (size_t)BUBBLE_COLS;
        if (i + take < n && text[i + take] != ' ') {
            size_t brk = take;
            while (brk > 0 && text[i + brk - 1] != ' ') --brk;
            if (brk > 0) take = brk - 1;   // stop before the space rather than printing it
        }
        std::memcpy(lines[l], text + i, take);
        lines[l][take] = '\0';
        i += take;
    }
    bubble_ = line1_[0] != '\0';
    bubble_until_ms_ = bubble_ ? t_ms_ + hold_ms : 0;
    ++bubble_id_;
}
void Face::clear_say() { bubble_ = false; bubble_until_ms_ = 0; }

void Face::step(int16_t& v, int16_t target, uint32_t dt_ms) {
    int diff = target - v;
    if (diff == 0) return;
    int s = (diff * (int)dt_ms) / 120;          // ~120 ms to traverse, ease-out
    if (s == 0) s = diff > 0 ? 1 : -1;
    v = (int16_t)(v + s);
    if ((diff > 0 && v > target) || (diff < 0 && v < target)) v = target;
}
Rect Face::eye_bounds(int cx, const EyeShape& e) {
    return Rect{(int16_t)(cx - e.rx - 2), (int16_t)(EYE_CY - e.ry - 2), (int16_t)(2 * e.rx + 5), (int16_t)(2 * e.ry + 5)};
}
void Face::brow_ends(int cx, bool left_eye, const BrowShape& b, int& x0, int& y0, int& x1, int& y1) {
    int y = EYE_CY + BROW_DY - b.raise;
    // tan(deg) ~ deg * 0.01745 over the small angles brows use. The brow turns about its own
    // centre, so the inner end rises exactly as far as the outer end drops.
    int tilt = (BROW_HALF * b.angle_deg * 1745) / 100000;
    int inner_y = y - tilt, outer_y = y + tilt;
    x0 = cx - BROW_HALF; x1 = cx + BROW_HALF;
    if (left_eye) { y0 = outer_y; y1 = inner_y; }   // the inner end is the one nearest the nose
    else          { y0 = inner_y; y1 = outer_y; }
}
Rect Face::brow_bounds(int cx, const BrowShape& b) {
    int x0, y0, x1, y1;
    brow_ends(cx, true, b, x0, y0, x1, y1);         // the box is the same whichever end tilts
    int lo = std::min(y0, y1) - BROW_THICK, hi = std::max(y0, y1) + BROW_THICK;
    return Rect{(int16_t)(x0 - BROW_THICK), (int16_t)lo, (int16_t)(x1 - x0 + 2 * BROW_THICK), (int16_t)(hi - lo + 1)};
}
// One box covering every mouth shape. The mouth changes rarely and the box is small;
// a tight rect per kind would save a few hundred pixels of SPI and cost real complexity
// in the erase path.
Rect Face::mouth_bounds() { return Rect{118, 130, 84, 69}; }
Rect Face::glyph_bounds() { return Rect{276, 70, 34, 36}; }

void Face::draw_eye(Framebuffer& fb, int cx, const EyeShape& e, uint16_t bg) {
    fb.fill_ellipse(cx, EYE_CY, e.rx, e.ry, EYE);
    fb.fill_ellipse(cx + e.pupil_dx, EYE_CY + e.pupil_dy, e.pupil_r, e.pupil_r, PUPIL);
    int top = (2 * e.ry * e.lid_top_pct) / 100, bottom = (2 * e.ry * e.lid_bottom_pct) / 100;
    // top+2, not top+1: the ellipse spans 2*ry+1 rows (dy from -ry to ry inclusive), so a
    // fully shut lid (top == 2*ry) must cover 2*ry+1 rows or it leaves the single-pixel
    // bottom-most row of the arc undrawn. Matches the bottom rect's +2 below.
    if (top > 0) fb.fill_rect(Rect{(int16_t)(cx - e.rx - 1), (int16_t)(EYE_CY - e.ry - 1), (int16_t)(2 * e.rx + 3), (int16_t)(top + 2)}, bg);
    if (bottom > 0) fb.fill_rect(Rect{(int16_t)(cx - e.rx - 1), (int16_t)(EYE_CY + e.ry - bottom), (int16_t)(2 * e.rx + 3), (int16_t)(bottom + 2)}, bg);
}
void Face::draw_brow(Framebuffer& fb, int cx, bool left_eye, const BrowShape& b) {
    int x0, y0, x1, y1;
    brow_ends(cx, left_eye, b, x0, y0, x1, y1);
    draw_line(fb, x0, y0, x1, y1, LINE, BROW_THICK);
}
void Face::draw_mouth(Framebuffer& fb, const MouthShape& m, uint16_t bg) {
    int w = m.width < 8 ? 8 : m.width, r = w / 2;
    switch (m.kind) {
        case MouthKind::Flat: draw_line(fb, MOUTH_CX - r, MOUTH_CY, MOUTH_CX + r, MOUTH_CY, LINE, MOUTH_THICK); break;
        case MouthKind::Smile: draw_arc(fb, MOUTH_CX, MOUTH_CY, r, 20, 160, LINE, MOUTH_THICK); break;
        case MouthKind::Frown: draw_arc(fb, MOUTH_CX, MOUTH_CY, r, 200, 340, LINE, MOUTH_THICK); break;
        case MouthKind::O:
            draw_disc(fb, MOUTH_CX, MOUTH_CY, 12, LINE);
            draw_disc(fb, MOUTH_CX, MOUTH_CY, 7, bg);
            break;
        case MouthKind::Open: {
            int h = 4 + (m.open_pct * 20) / 100;
            draw_round_rect(fb, Rect{(int16_t)(MOUTH_CX - r), (int16_t)(MOUTH_CY - h / 2), (int16_t)(2 * r), (int16_t)h}, h / 2, LINE, LINE);
            break;
        }
    }
}
void Face::glyph_heights(int16_t out[3]) const {
    for (int i = 0; i < 3; ++i) {
        unsigned p = (t_ms_ + (unsigned)i * 133u) % 400u;       // 400 ms cycle, bars out of phase
        int tri = p < 200 ? (int)p / 10 : (int)(400 - p) / 10;  // 0..20
        if (listen_level_ >= 0) tri = (tri * listen_level_) / 100;
        out[i] = (int16_t)(6 + tri);
    }
}
void Face::draw_glyph(Framebuffer& fb, const int16_t h[3]) const {
    static const int xs[3] = {282, 292, 302};
    for (int i = 0; i < 3; ++i)
        fb.fill_rect(Rect{(int16_t)(xs[i] - 3), (int16_t)(EYE_CY - h[i] / 2), 7, h[i]}, LINE);
}
void Face::draw_bubble(Framebuffer& fb) const {
    draw_round_rect(fb, bubble_rect(), 8, BUBBLE_FILL, BUBBLE_BORDER);
    draw_text(fb, 16, 148, line1_, 2, BUBBLE_TEXT);
    if (line2_[0]) draw_text(fb, 16, 166, line2_, 2, BUBBLE_TEXT);
}
void Face::draw_all(Framebuffer& fb, const FaceShape& show, const int16_t glyph[3], bool want_glyph) const {
    draw_eye(fb, LEFT_CX, show.left, show.bg);
    draw_eye(fb, RIGHT_CX, show.right, show.bg);
    draw_brow(fb, LEFT_CX, true, show.lbrow);
    draw_brow(fb, RIGHT_CX, false, show.rbrow);
    if (!bubble_) draw_mouth(fb, show.mouth, show.bg);
    if (want_glyph) draw_glyph(fb, glyph);
    if (bubble_) draw_bubble(fb);
}

Rect Face::tick(uint32_t dt_ms, Framebuffer& fb) {
    t_ms_ += dt_ms;
    if (hold_until_ms_ && t_ms_ >= hold_until_ms_) { hold_until_ms_ = 0; apply_emotion(night_ ? Emotion::Sleepy : Emotion::Idle); }
    if (bubble_ && bubble_until_ms_ && t_ms_ >= bubble_until_ms_) { bubble_ = false; bubble_until_ms_ = 0; }
    if (talking_) talk_ms_ += dt_ms;

    // Idle life. Both schedules run off deterministic pseudo-random gaps, so a test can step
    // a fixed number of frames and know what it will see.
    Emotion e = target_emotion_;
    bool restful = !talking_ && (e == Emotion::Idle || e == Emotion::Happy || e == Emotion::Sleepy);
    bool may_blink = restful || (!talking_ && e == Emotion::Listening);
    if (may_blink) {
        if (!blinking_ && t_ms_ >= next_blink_ms_) { blinking_ = true; blink_until_ms_ = t_ms_ + 120; }
        if (blinking_ && t_ms_ >= blink_until_ms_) {
            blinking_ = false; ++blink_n_;
            uint32_t gap = 2800 + (blink_n_ * 577u) % 1900u;
            if (e == Emotion::Sleepy) gap *= 2;      // a sleepy face blinks half as often
            next_blink_ms_ = t_ms_ + gap;
        }
    } else if (blinking_) { blinking_ = false; next_blink_ms_ = t_ms_ + 2800; }
    if (restful) {
        if (!sacc_until_ms_ && t_ms_ >= next_sacc_ms_) {
            sacc_until_ms_ = t_ms_ + 300;
            sacc_dx_ = (sacc_n_ & 1u) ? 6 : -6;
            sacc_dy_ = (sacc_n_ & 2u) ? 3 : -3;
            retarget();
        } else if (sacc_until_ms_ && t_ms_ >= sacc_until_ms_) {
            sacc_until_ms_ = 0; ++sacc_n_;
            sacc_dx_ = sacc_dy_ = 0;
            next_sacc_ms_ = t_ms_ + 2000 + (sacc_n_ * 733u) % 3000u;
            retarget();
        }
    } else if (sacc_dx_ || sacc_dy_ || sacc_until_ms_) {
        sacc_dx_ = sacc_dy_ = 0; sacc_until_ms_ = 0; next_sacc_ms_ = t_ms_ + 2000; retarget();
    }

    auto tween_eye = [&](EyeShape& c, const EyeShape& t) {
        step(c.rx, t.rx, dt_ms); step(c.ry, t.ry, dt_ms);
        step(c.lid_top_pct, t.lid_top_pct, dt_ms); step(c.lid_bottom_pct, t.lid_bottom_pct, dt_ms);
        step(c.pupil_dx, t.pupil_dx, dt_ms); step(c.pupil_dy, t.pupil_dy, dt_ms); step(c.pupil_r, t.pupil_r, dt_ms);
    };
    tween_eye(cur_.left, target_.left);
    tween_eye(cur_.right, target_.right);
    step(cur_.lbrow.angle_deg, target_.lbrow.angle_deg, dt_ms); step(cur_.lbrow.raise, target_.lbrow.raise, dt_ms);
    step(cur_.rbrow.angle_deg, target_.rbrow.angle_deg, dt_ms); step(cur_.rbrow.raise, target_.rbrow.raise, dt_ms);
    step(cur_.mouth.width, target_.mouth.width, dt_ms);
    cur_.mouth.kind = target_.mouth.kind;        // a mouth does not morph between kinds, it changes
    cur_.mouth.open_pct = target_.mouth.open_pct;
    cur_.bg = target_.bg;                        // the mood tint switches at once, with a full repaint

    FaceShape show = cur_;
    if (blinking_) { show.left.lid_top_pct = 100; if (target_emotion_ != Emotion::Wink) show.right.lid_top_pct = 100; }
    // Talking drives the mouth straight rather than through the tween: 120 ms a half-cycle is
    // shorter than the tween, so a tweened mouth would never reach either end.
    if (talking_) show.mouth.open_pct = (int16_t)(((talk_ms_ / 120u) % 2u) ? 20 : 100);
    int16_t glyph[3] = {0, 0, 0};
    bool want_glyph = !talking_ && target_emotion_ == Emotion::Listening;
    if (want_glyph) glyph_heights(glyph);

    if (first_ || show.bg != drawn_.bg) {
        fb.fill_rect(Rect{0, 0, Framebuffer::W, FACE_H}, show.bg);
        draw_all(fb, show, glyph, want_glyph);
        first_ = false;
        drawn_ = show; std::memcpy(drawn_glyph_, glyph, sizeof glyph);
        drawn_bubble_ = bubble_; drawn_bubble_id_ = bubble_id_;
        return Rect{0, 0, Framebuffer::W, FACE_H};
    }

    Rect dirty{0, 0, 0, 0};
    if (!(show.left == drawn_.left)) dirty = rect_union(dirty, rect_union(eye_bounds(LEFT_CX, drawn_.left), eye_bounds(LEFT_CX, show.left)));
    if (!(show.right == drawn_.right)) dirty = rect_union(dirty, rect_union(eye_bounds(RIGHT_CX, drawn_.right), eye_bounds(RIGHT_CX, show.right)));
    if (!(show.lbrow == drawn_.lbrow)) dirty = rect_union(dirty, rect_union(brow_bounds(LEFT_CX, drawn_.lbrow), brow_bounds(LEFT_CX, show.lbrow)));
    if (!(show.rbrow == drawn_.rbrow)) dirty = rect_union(dirty, rect_union(brow_bounds(RIGHT_CX, drawn_.rbrow), brow_bounds(RIGHT_CX, show.rbrow)));
    if (!(show.mouth == drawn_.mouth)) dirty = rect_union(dirty, mouth_bounds());
    if (std::memcmp(glyph, drawn_glyph_, sizeof glyph) != 0) dirty = rect_union(dirty, glyph_bounds());
    if (bubble_ != drawn_bubble_ || bubble_id_ != drawn_bubble_id_) dirty = rect_union(dirty, rect_union(bubble_rect(), mouth_bounds()));
    dirty = clamp_face(dirty);
    if (dirty.empty()) return dirty;

    // Erase the dirty box, then redraw every part. Parts outside the box repaint themselves
    // identically, which costs a little RAM bandwidth and removes a class of ordering bug
    // where two overlapping parts (bubble over mouth) disagree about who owns a pixel.
    fb.fill_rect(dirty, show.bg);
    draw_all(fb, show, glyph, want_glyph);
    drawn_ = show; std::memcpy(drawn_glyph_, glyph, sizeof glyph);
    drawn_bubble_ = bubble_; drawn_bubble_id_ = bubble_id_;
    return dirty;
}
}
