#include "pip/hud.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include "pip/draw.hpp"
namespace pip {
namespace {
constexpr int CAPTION_SCALE = 2;
// The stats line is 2x too: 1x (the spec's first guess) is unreadable on film from a
// 2.8" panel. Labels lose their space so the worst case ("25C rfx1.2ms jdg850ms", 21
// characters, 250 px) still fits from x 64 on a 320 px panel next to a 40 px lux bar.
constexpr int STATS_SCALE = 2;

// 8x8 pixel icons, MSB left. Drawn at the caption scale, they are the "emoji"
// a 320x240 panel can honestly render.
constexpr uint8_t ICONS[][8] = {
    // robot: antenna, square head, two eyes, mouth
    {0b00011000, 0b00011000, 0b01111110, 0b01011010, 0b01111110, 0b01100110, 0b01111110, 0b00100100},
    // gear: four teeth around a ring
    {0b00011000, 0b01111110, 0b01100110, 0b11000011, 0b11000011, 0b01100110, 0b01111110, 0b00011000},
    // magnifier: lens upper-left, handle to lower-right
    {0b00111000, 0b01000100, 0b01000100, 0b01000100, 0b00111010, 0b00000101, 0b00000010, 0b00000000},
    // eye: almond with a pupil
    {0b00000000, 0b00111100, 0b01111110, 0b11100111, 0b11100111, 0b01111110, 0b00111100, 0b00000000},
    // speaker: cone plus sound waves
    {0b00001001, 0b00011010, 0b01111001, 0b01111101, 0b01111001, 0b00011010, 0b00001001, 0b00000000},
    // microphone: capsule on a stem
    {0b00111000, 0b00111000, 0b00111000, 0b00111000, 0b00010000, 0b00111000, 0b00010000, 0b00111000},
    // lightning bolt
    {0b00001100, 0b00011000, 0b00110000, 0b01111100, 0b00011000, 0b00110000, 0b01100000, 0b01000000},
    // shield: wide top tapering to a point
    {0b11111111, 0b11111111, 0b11111111, 0b01111110, 0b01111110, 0b00111100, 0b00111100, 0b00011000},
};

void draw_icon(pip::Framebuffer& fb, int x, int y, int idx, int scale, uint16_t colour) {
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c)
            if (ICONS[idx][r] & (0x80u >> c))
                fb.fill_rect(pip::Rect{(int16_t)(x + c * scale), (int16_t)(y + r * scale), (int16_t)scale, (int16_t)scale}, colour);
}

bool starts(const char* s, const char* p) { return std::strncmp(s, p, std::strlen(p)) == 0; }
}

Hud::CaptionStyle Hud::caption_style(const char* scene) {
    if (starts(scene, "deep-agent")) return {AGENT, ICON_ROBOT};
    if (starts(scene, "tool search")) return {TOOL, ICON_SEARCH};
    if (starts(scene, "tool look")) return {TOOL, ICON_EYE};
    if (starts(scene, "tool say")) return {TOOL, ICON_SPEAK};
    if (starts(scene, "tool ")) return {TOOL, ICON_GEAR};
    if (starts(scene, "rete ")) return {RETE, ICON_BOLT};
    if (starts(scene, "guard ")) return {GUARD, ICON_SHIELD};
    if (starts(scene, "ears ")) return {EARS, ICON_MIC};
    return {FG, ICON_NONE};
}

void Hud::apply(const HudUpdate& u) {
    if (u.has_reflex_us) s_.reflex_us = u.reflex_us;
    if (u.has_judge_ms) s_.judge_ms = u.judge_ms;
    if (u.has_brain) s_.brain = u.brain;
    if (u.has_cortex) s_.cortex = u.cortex;
    if (u.has_mind) s_.mind = u.mind;
    if (u.has_scene) std::snprintf(s_.scene, sizeof s_.scene, "%s", u.scene);
}
void Hud::set_senses(float lux, bool night, float temp_c, bool wire, bool wifi) {
    s_.lux = lux; s_.night = night; s_.temp_c = temp_c; s_.wire = wire; s_.wifi = wifi;
}
void Hud::fmt_us(long us, char* out, size_t cap) {
    if (us < 0) { std::snprintf(out, cap, "--"); return; }
    if (us < 1000) { std::snprintf(out, cap, "%ldus", us); return; }
    if (us < 100000) { std::snprintf(out, cap, "%ld.%ldms", us / 1000, (us % 1000) / 100); return; }
    // Past 100 ms (HTTP path, or a stalled wire) the decimal buys nothing and the stats
    // line must stay under 22 characters to fit the panel.
    if (us < 1000000) { std::snprintf(out, cap, "%ldms", us / 1000); return; }
    std::snprintf(out, cap, "%ld.%lds", us / 1000000, (us % 1000000) / 100000);
}
void Hud::fmt_ms(long ms, char* out, size_t cap) {
    if (ms < 0) { std::snprintf(out, cap, "--"); return; }
    if (ms < 1000) { std::snprintf(out, cap, "%ldms", ms); return; }
    std::snprintf(out, cap, "%ld.%lds", ms / 1000, (ms % 1000) / 100);
}
Hud::Render Hud::make_render() const {
    Render r{};
    std::snprintf(r.scene, sizeof r.scene, "%s", s_.scene);
    fmt_us(s_.reflex_us, r.us, sizeof r.us);
    fmt_ms(s_.judge_ms, r.ms, sizeof r.ms);
    float t = std::isfinite(s_.temp_c) ? s_.temp_c : 0.0f;
    if (t < -99.0f) t = -99.0f; else if (t > 199.0f) t = 199.0f;
    std::snprintf(r.temp, sizeof r.temp, "%.0f", (double)t);
    // Log scale, 0..200 lux: the interesting range for "is the room dark" is the bottom
    // decade, which a linear bar throws away.
    float lux = std::isfinite(s_.lux) && s_.lux > 0 ? s_.lux : 0.0f;
    float f = std::log10(1.0f + lux) / std::log10(201.0f);
    if (f < 0) f = 0; else if (f > 1) f = 1;
    int px = (int)(f * (BAR_W - 2) + 0.5f);
    r.bar_px = (uint8_t)(px < 0 ? 0 : (px > BAR_W - 2 ? BAR_W - 2 : px));
    r.flags = (uint8_t)((s_.wire ? 1 : 0) | (s_.wifi ? 2 : 0) | (s_.brain ? 4 : 0) | (s_.cortex ? 8 : 0) | (s_.night ? 16 : 0));
    r.mind = s_.mind;
    return r;
}
Rect Hud::draw(Framebuffer& fb, bool force) {
    Render r = make_render();
    if (!first_ && !force && std::memcmp(&r, &drawn_, sizeof r) == 0) return Rect{0, 0, 0, 0};
    Rect strip{0, (int16_t)Y0, (int16_t)Framebuffer::W, (int16_t)H};
    fb.fill_rect(strip, BG);

    // Top line: what Pip is doing on the left, who is alive on the right.
    if (r.scene[0]) {
        CaptionStyle st = caption_style(r.scene);
        int tx = CAPTION_X;
        if (st.icon != ICON_NONE) {
            draw_icon(fb, CAPTION_X, CAPTION_Y, st.icon, CAPTION_SCALE, st.colour);
            tx += 10 * CAPTION_SCALE;
        }
        draw_text(fb, tx, CAPTION_Y, r.scene, CAPTION_SCALE, st.colour);
    } else {
        draw_text(fb, CAPTION_X, CAPTION_Y, "idle", CAPTION_SCALE, DIM);
    }
    const char glyphs[5] = {'W', 'F', 'B', 'C', r.mind};
    const uint16_t colours[5] = {
        (r.flags & 1) ? OK : DIM, (r.flags & 2) ? OK : DIM,
        (r.flags & 4) ? OK : DIM, (r.flags & 8) ? OK : DIM,
        // The mind glyph says which box answered; give it that box's colour.
        r.mind == 'J' ? OK : (r.mind == '5' ? WARN : DIM),
    };
    int gx = Framebuffer::W - GLYPH_RIGHT - (6 * CAPTION_SCALE * 5 - CAPTION_SCALE);
    for (int i = 0; i < 5; ++i) {
        char one[2] = {glyphs[i], '\0'};
        draw_text(fb, gx + i * 6 * CAPTION_SCALE, GLYPH_Y, one, CAPTION_SCALE, colours[i]);
    }

    // Bottom line: how much light, how warm, how fast each half of the brain was.
    draw_round_rect(fb, Rect{BAR_X, BAR_Y, BAR_W, BAR_H}, 0, BG, DIM);
    if (r.bar_px > 0) fb.fill_rect(Rect{(int16_t)(BAR_X + 1), (int16_t)(BAR_Y + 1), (int16_t)r.bar_px, (int16_t)(BAR_H - 2)}, OK);
    if (r.flags & 16) {
        // A disc with a bite out of it: recognisably a moon at ten pixels across, and the
        // bite sits far enough right that it does not eat the disc's own centre.
        draw_disc(fb, MOON_CX, MOON_CY, 5, FG);
        draw_disc(fb, MOON_CX + 5, MOON_CY - 1, 4, BG);
    }
    char line[48];
    std::snprintf(line, sizeof line, "%sC rfx%s jdg%s", r.temp, r.us, r.ms);
    draw_text(fb, TEXT_X, TEXT_Y, line, STATS_SCALE, FG);

    drawn_ = r;
    first_ = false;
    return strip;
}
}
