#include "pip/face.hpp"
#include <algorithm>
#include <cstring>
namespace pip {
Rect rect_union(Rect a, Rect b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    int16_t x0 = std::min(a.x, b.x), y0 = std::min(a.y, b.y);
    int16_t x1 = std::max((int16_t)(a.x + a.w), (int16_t)(b.x + b.w)), y1 = std::max((int16_t)(a.y + a.h), (int16_t)(b.y + b.h));
    return Rect{x0, y0, (int16_t)(x1 - x0), (int16_t)(y1 - y0)};
}
void Framebuffer::fill(uint16_t c) { for (int i = 0; i < W * H; ++i) px[i] = c; }
void Framebuffer::fill_rect(Rect r, uint16_t c) {
    int x0 = std::max(0, (int)r.x), y0 = std::max(0, (int)r.y);
    int x1 = std::min(W, (int)r.x + (int)r.w), y1 = std::min(H, (int)r.y + (int)r.h);
    for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) px[y * W + x] = c;
}
void Framebuffer::fill_ellipse(int cx, int cy, int rx, int ry, uint16_t c) {
    if (rx <= 0 || ry <= 0) return;
    for (int dy = -ry; dy <= ry; ++dy) {
        int y = cy + dy; if (y < 0 || y >= H) continue;
        // half-width at this row: rx * sqrt(1 - (dy/ry)^2), in integer math
        long num = (long)rx * rx * ((long)ry * ry - (long)dy * dy);
        long den = (long)ry * ry;
        int hw = 0; long q = num / den; while ((long)(hw + 1) * (hw + 1) <= q) ++hw;
        int x0 = std::max(0, cx - hw), x1 = std::min(W - 1, cx + hw);
        for (int x = x0; x <= x1; ++x) px[y * W + x] = c;
    }
}
FaceShape shape_for(Emotion e) {
    EyeShape base{40, 50, 0, 0, 0, 0, 14};
    FaceShape s{base, base};
    switch (e) {
        case Emotion::Idle: break;
        case Emotion::Happy: s.left.ry = s.right.ry = 36; s.left.lid_bottom_pct = s.right.lid_bottom_pct = 45; break;
        case Emotion::Sleepy: s.left.lid_top_pct = s.right.lid_top_pct = 60; s.left.pupil_dy = s.right.pupil_dy = 8; break;
        case Emotion::Thinking: s.left.pupil_dx = s.right.pupil_dx = 14; s.left.pupil_dy = s.right.pupil_dy = -16; s.left.lid_top_pct = s.right.lid_top_pct = 15; break;
        case Emotion::Alert: s.left.rx = s.right.rx = 44; s.left.ry = s.right.ry = 58; s.left.pupil_r = s.right.pupil_r = 10; break;
        case Emotion::Wink: s.left.lid_top_pct = 100; break;
        default: break;
    }
    return s;
}
Face::Face() : cur_(shape_for(Emotion::Idle)), target_(cur_), drawn_(cur_) {}
void Face::set_emotion(Emotion e) { target_emotion_ = e; target_ = shape_for(e); }
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
void Face::draw_eye(Framebuffer& fb, int cx, const EyeShape& e) {
    fb.fill_ellipse(cx, EYE_CY, e.rx, e.ry, EYE);
    fb.fill_ellipse(cx + e.pupil_dx, EYE_CY + e.pupil_dy, e.pupil_r, e.pupil_r, PUPIL);
    int top = (2 * e.ry * e.lid_top_pct) / 100, bottom = (2 * e.ry * e.lid_bottom_pct) / 100;
    // top+2, not top+1: the ellipse spans 2*ry+1 rows (dy from -ry to ry inclusive), so a
    // fully shut lid (top == 2*ry) must cover 2*ry+1 rows or it leaves the single-pixel
    // bottom-most row of the arc undrawn. Matches the bottom rect's +2 below.
    if (top > 0) fb.fill_rect(Rect{(int16_t)(cx - e.rx - 1), (int16_t)(EYE_CY - e.ry - 1), (int16_t)(2 * e.rx + 3), (int16_t)(top + 2)}, BG);
    if (bottom > 0) fb.fill_rect(Rect{(int16_t)(cx - e.rx - 1), (int16_t)(EYE_CY + e.ry - bottom), (int16_t)(2 * e.rx + 3), (int16_t)(bottom + 2)}, BG);
}
Rect Face::tick(uint32_t dt_ms, Framebuffer& fb) {
    t_ms_ += dt_ms;
    auto both = [&](auto fn) { fn(cur_.left, target_.left); fn(cur_.right, target_.right); };
    both([&](EyeShape& c, const EyeShape& t) {
        step(c.rx, t.rx, dt_ms); step(c.ry, t.ry, dt_ms);
        step(c.lid_top_pct, t.lid_top_pct, dt_ms); step(c.lid_bottom_pct, t.lid_bottom_pct, dt_ms);
        step(c.pupil_dx, t.pupil_dx, dt_ms); step(c.pupil_dy, t.pupil_dy, dt_ms); step(c.pupil_r, t.pupil_r, dt_ms);
    });
    // Blink schedule: pseudo-random gaps from a counter so tests are deterministic.
    if (!blinking_ && t_ms_ >= next_blink_ms_) { blinking_ = true; blink_until_ms_ = t_ms_ + 120; }
    if (blinking_ && t_ms_ >= blink_until_ms_) { blinking_ = false; ++blink_n_; next_blink_ms_ = t_ms_ + 2800 + (blink_n_ * 577u) % 1900u; }
    FaceShape show = cur_;
    if (blinking_) { show.left.lid_top_pct = 100; if (target_emotion_ != Emotion::Wink) show.right.lid_top_pct = 100; }
    bool changed = first_ || std::memcmp(&show, &drawn_, sizeof show) != 0;
    if (!changed) return Rect{0, 0, 0, 0};
    Rect dirty;
    if (first_) {
        fb.fill(BG);
        dirty = Rect{0, 0, Framebuffer::W, Framebuffer::H};
        first_ = false;
    } else {
        dirty = rect_union(rect_union(eye_bounds(LEFT_CX, drawn_.left), eye_bounds(LEFT_CX, show.left)),
                           rect_union(eye_bounds(RIGHT_CX, drawn_.right), eye_bounds(RIGHT_CX, show.right)));
        fb.fill_rect(dirty, BG);
    }
    draw_eye(fb, LEFT_CX, show.left);
    draw_eye(fb, RIGHT_CX, show.right);
    drawn_ = show;
    return dirty;
}
}
