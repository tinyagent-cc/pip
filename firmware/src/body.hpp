#pragma once
#include <atomic>
#include <cstdio>
#include <cstring>
#include "pip/protocol.hpp"
namespace pip {
// Protocol handlers run inside lwIP callbacks and inside the UART drain; they only drop
// values here. The main loop drains them once per frame. Senses are published by the main
// loop and read here.
class RealBody : public Body {
public:
    void express(Emotion e) override { pending_emotion_.store((int)e); }
    void chirp(Chirp c) override { pending_chirp_.store((int)c); }
    void led(uint8_t r, uint8_t g, uint8_t b) override { pending_led_.store(0x01000000u | (r << 16) | (g << 8) | b); }
    void say(const char* text) override { std::snprintf(say_, sizeof say_, "%s", text); have_say_.store(true); }
    void scene(const char* name) override { std::snprintf(scene_, sizeof scene_, "%s", name); have_scene_.store(true); }
    // Two pushes arriving between two frames must not lose the first one's fields: a HUD
    // update is a set of optional values, so merge rather than overwrite.
    void hud(const HudUpdate& u) override {
        if (u.has_reflex_us) { hud_.has_reflex_us = true; hud_.reflex_us = u.reflex_us; }
        if (u.has_judge_ms) { hud_.has_judge_ms = true; hud_.judge_ms = u.judge_ms; }
        if (u.has_brain) { hud_.has_brain = true; hud_.brain = u.brain; }
        if (u.has_cortex) { hud_.has_cortex = true; hud_.cortex = u.cortex; }
        if (u.has_mind) { hud_.has_mind = true; hud_.mind = u.mind; }
        if (u.has_scene) { hud_.has_scene = true; std::memcpy(hud_.scene, u.scene, sizeof hud_.scene); }
        have_hud_.store(true);
    }
    Senses senses() override { return Senses{lux_.load(), temp_.load(), button_.load(), link_, audio_}; }
    void publish(float lux, float temp_c, bool button) { lux_.store(lux); temp_.store(temp_c); button_.store(button); }
    void publish_link(const LinkStats& l, const AudioStats& a) { link_ = l; audio_ = a; }
    bool take_emotion(Emotion& e) { int v = pending_emotion_.exchange(-1); if (v < 0) return false; e = (Emotion)v; return true; }
    bool take_chirp(Chirp& c) { int v = pending_chirp_.exchange(-1); if (v < 0) return false; c = (Chirp)v; return true; }
    bool take_led(uint8_t& r, uint8_t& g, uint8_t& b) { uint32_t v = pending_led_.exchange(0); if (!(v & 0x01000000u)) return false; r = (v >> 16) & 0xFF; g = (v >> 8) & 0xFF; b = v & 0xFF; return true; }
    bool take_say(char* out, size_t cap) { if (!have_say_.exchange(false)) return false; std::snprintf(out, cap, "%s", say_); return true; }
    bool take_scene(char* out, size_t cap) { if (!have_scene_.exchange(false)) return false; std::snprintf(out, cap, "%s", scene_); return true; }
    bool take_hud(HudUpdate& out) { if (!have_hud_.exchange(false)) return false; out = hud_; hud_ = HudUpdate{}; return true; }
private:
    std::atomic<int> pending_emotion_{-1}, pending_chirp_{-1};
    std::atomic<uint32_t> pending_led_{0};
    std::atomic<float> lux_{-1.0f}, temp_{0.0f};
    std::atomic<bool> button_{false};
    std::atomic<bool> have_say_{false}, have_scene_{false}, have_hud_{false};
    char say_[96] = {0}, scene_[16] = {0};
    HudUpdate hud_{};
    LinkStats link_{};
    AudioStats audio_{};
};
static_assert(std::atomic<float>::is_always_lock_free && std::atomic<uint32_t>::is_always_lock_free, "mailbox must be lock-free");
}
