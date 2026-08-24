#pragma once
#include <cstdint>

namespace pip {

// Keeps a stalled radio chip from starving the body's main loop.
//
// A healthy CYW43 transaction takes microseconds; a stalled chip blocks each one
// for ~1 s of driver timeout, which overruns the 45 ms UART ring and chops audio.
// The loop times every radio touch and reports it here. One stalled call puts the
// radio in quarantine: allow() goes false, so the loop stops touching the chip and
// runs at full speed with the radio simply offline. When the quarantine expires,
// should_reset() fires exactly once -- the caller power-cycles the chip
// (cyw43_arch_deinit/init) and reports how long that took. A fast reset means the
// chip is back; a stalled one starts the next quarantine.
class RadioGuard {
public:
    // A genuine chip stall blocks ~1 s per transaction (the driver's STALL timeout).
    // Healthy calls are microseconds, but during association the lwIP lock can hold a
    // caller for tens of ms -- 300 ms tells the two apart with margin on both sides.
    static constexpr uint32_t kStallMs = 300;
    static constexpr uint32_t kQuarantineMs = 60000;

    // Duration of any radio call, the reset attempt included.
    void report(uint32_t now_ms, uint32_t took_ms) {
        if (took_ms < kStallMs) return;
        sick_ = true;
        until_ms_ = now_ms + kQuarantineMs;
        ++stalls_;
    }

    // May the loop touch the radio this frame? Stays false through the whole
    // quarantine, expiry included: the reset happens before normal traffic resumes.
    bool allow(uint32_t now_ms) const {
        (void)now_ms;
        return !sick_;
    }

    // True exactly once per expired quarantine; consuming it puts the radio on
    // probation -- the reset's own report() re-judges it.
    bool should_reset(uint32_t now_ms) {
        if (!sick_ || (int32_t)(now_ms - until_ms_) < 0) return false;
        sick_ = false;
        return true;
    }

    uint32_t stalls() const { return stalls_; }

private:
    bool sick_ = false;
    uint32_t until_ms_ = 0;
    uint32_t stalls_ = 0;
};

}  // namespace pip
