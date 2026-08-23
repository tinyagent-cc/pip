#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>

namespace pip::brain {
// Knobs the reflex rules and the LLM guardrails share. Thread-safe where two
// threads can touch it (night is read by HTTP handlers for /health).
struct Policy {
    std::atomic<bool> night{false};
    int night_cap = 40;        // max LED channel value while the room is dark
    int chirp_gap_ms = 5000;   // minimum spacing between chirps
    double hot_c = 35.0;       // temp.hot threshold

    int clamp(int v) const {
        v = std::clamp(v, 0, 255);
        return night.load() ? std::min(v, night_cap) : v;
    }
    // Grants or denies a chirp at now_ms; a grant is remembered.
    bool chirp_allowed(int64_t now_ms) {
        std::lock_guard<std::mutex> g(m_);
        if (last_chirp_ms_ >= 0 && now_ms - last_chirp_ms_ < chirp_gap_ms) return false;
        last_chirp_ms_ = now_ms;
        return true;
    }
private:
    std::mutex m_;
    int64_t last_chirp_ms_ = -1;
};
}  // namespace pip::brain
