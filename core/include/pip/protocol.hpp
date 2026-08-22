#pragma once
#include <cstddef>
#include <cstdint>
#include "pip/http.hpp"
namespace pip {
enum class Emotion : uint8_t { Idle, Happy, Sleepy, Thinking, Alert, Wink, Count };
enum class Chirp : uint8_t { Rise, Trill, Drop, Purr, Count };
const char* emotion_name(Emotion e);
bool emotion_from(const char* s, Emotion& out);
const char* chirp_name(Chirp c);
bool chirp_from(const char* s, Chirp& out);
struct Senses { float light_lux; float temp_c; bool button_down; };
// What the protocol drives. Implementations must be cheap and safe to call from an lwIP
// callback: set a pending value, let the main loop act on it.
struct Body {
    virtual ~Body() = default;
    virtual void express(Emotion e) = 0;
    virtual void chirp(Chirp c) = 0;
    virtual void led(uint8_t r, uint8_t g, uint8_t b) = 0;
    virtual Senses senses() = 0;
};
// Routes one parsed request per PROTOCOL.md v0 and writes the full HTTP response. Returns bytes.
size_t handle_request(const http::Request& req, Body& body, char* out, size_t cap);
// {"event":"<name>"} for POST <brain>/event. Returns bytes.
size_t event_json(const char* event, char* out, size_t cap);
}
