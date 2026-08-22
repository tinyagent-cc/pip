#include "pip/events.hpp"
namespace pip {
const char* event_name(Event e) {
    switch (e) {
        case Event::ButtonPress: return "button.press";
        case Event::ButtonHold: return "button.hold";
        case Event::ButtonRelease: return "button.release";
        case Event::LightLow: return "light.low";
        case Event::LightHigh: return "light.high";
        default: return nullptr;
    }
}
Event ButtonFsm::tick(uint32_t now, bool pressed) {
    if (pressed != raw_) { raw_ = pressed; raw_since_ = now; }
    if (raw_ != stable_ && now - raw_since_ >= debounce_ms_) {
        stable_ = raw_;
        if (stable_) { press_at_ = now; hold_sent_ = false; return Event::ButtonPress; }
        return Event::ButtonRelease;
    }
    if (stable_ && !hold_sent_ && now - press_at_ >= hold_ms_) { hold_sent_ = true; return Event::ButtonHold; }
    return Event::None;
}
Event LightFsm::tick(uint32_t now, float lux) {
    if (!low_state_) {
        if (lux < low_) {
            if (!timing_) { timing_ = true; below_since_ = now; }
            else if (now - below_since_ >= sustain_) { low_state_ = true; timing_ = false; return Event::LightLow; }
        } else {
            timing_ = false;
        }
    } else if (lux > high_) {
        low_state_ = false;
        return Event::LightHigh;
    }
    return Event::None;
}
}
