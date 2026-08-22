#pragma once
#include <cstdint>
namespace pip {
enum class Event : uint8_t { None, ButtonPress, ButtonHold, ButtonRelease, LightLow, LightHigh };
const char* event_name(Event e);   // "button.press" etc., nullptr for None
// Debounced button with a one-shot hold. Feed it the raw pin state every few ms.
class ButtonFsm {
public:
    explicit ButtonFsm(uint32_t hold_ms = 1500, uint32_t debounce_ms = 30) : hold_ms_(hold_ms), debounce_ms_(debounce_ms) {}
    Event tick(uint32_t now_ms, bool pressed);
    bool down() const { return stable_; }
private:
    uint32_t hold_ms_, debounce_ms_;
    bool raw_ = false, stable_ = false, hold_sent_ = false;
    uint32_t raw_since_ = 0, press_at_ = 0;
};
// light.low once lux stays under low_lux for sustain_ms; light.high once it rises over high_lux.
class LightFsm {
public:
    LightFsm(float low_lux = 10.0f, float high_lux = 20.0f, uint32_t sustain_ms = 30000) : low_(low_lux), high_(high_lux), sustain_(sustain_ms) {}
    Event tick(uint32_t now_ms, float lux);
    bool is_low() const { return low_state_; }
private:
    float low_, high_;
    uint32_t sustain_;
    bool low_state_ = false, timing_ = false;
    uint32_t below_since_ = 0;
};
}
