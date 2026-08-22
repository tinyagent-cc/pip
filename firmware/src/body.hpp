#pragma once
#include <atomic>
#include "pip/protocol.hpp"
namespace pip {
// Protocol handlers run inside lwIP callbacks; they only drop values here. The main loop
// drains them once per frame. Senses are published by the main loop and read here.
class RealBody : public Body {
public:
    void express(Emotion e) override { pending_emotion_.store((int)e); }
    void chirp(Chirp c) override { pending_chirp_.store((int)c); }
    void led(uint8_t r, uint8_t g, uint8_t b) override { pending_led_.store(0x01000000u | (r << 16) | (g << 8) | b); }
    Senses senses() override { return Senses{lux_.load(), temp_.load(), button_.load()}; }
    void publish(float lux, float temp_c, bool button) { lux_.store(lux); temp_.store(temp_c); button_.store(button); }
    bool take_emotion(Emotion& e) { int v = pending_emotion_.exchange(-1); if (v < 0) return false; e = (Emotion)v; return true; }
    bool take_chirp(Chirp& c) { int v = pending_chirp_.exchange(-1); if (v < 0) return false; c = (Chirp)v; return true; }
    bool take_led(uint8_t& r, uint8_t& g, uint8_t& b) { uint32_t v = pending_led_.exchange(0); if (!(v & 0x01000000u)) return false; r = (v >> 16) & 0xFF; g = (v >> 8) & 0xFF; b = v & 0xFF; return true; }
private:
    std::atomic<int> pending_emotion_{-1}, pending_chirp_{-1};
    std::atomic<uint32_t> pending_led_{0};
    std::atomic<float> lux_{-1.0f}, temp_{0.0f};
    std::atomic<bool> button_{false};
};
static_assert(std::atomic<float>::is_always_lock_free && std::atomic<uint32_t>::is_always_lock_free, "mailbox must be lock-free");
}
