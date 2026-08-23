#pragma once
#include <cstddef>
#include <cstdint>
#include "pip/http.hpp"
namespace pip {
enum class Emotion : uint8_t { Idle, Happy, Sleepy, Thinking, Alert, Wink, Surprised, Sad, Listening, Talking, Count };
enum class Chirp : uint8_t { Rise, Trill, Drop, Purr, Count };
const char* emotion_name(Emotion e);
bool emotion_from(const char* s, Emotion& out);
const char* chirp_name(Chirp c);
bool chirp_from(const char* s, Chirp& out);
struct LinkStats { uint32_t rx_frames = 0, rx_bad = 0, audio_dropped = 0; };
struct AudioStats { uint32_t free_bytes = 0; bool playing = false; };
struct Senses { float light_lux; float temp_c; bool button_down; LinkStats link; AudioStats audio; };
// A HUD push carries whatever the brain knows right now; every field is optional and the
// has_* flags say which arrived, so an omitted field keeps its last value on the strip.
struct HudUpdate {
    bool has_reflex_us = false, has_judge_ms = false, has_brain = false, has_cortex = false, has_mind = false, has_scene = false;
    long reflex_us = 0, judge_ms = 0;
    bool brain = false, cortex = false;
    char mind = ' ';
    char scene[16] = {0};
};
// What the protocol drives. Implementations must be cheap and safe to call from an lwIP
// callback or a UART drain: set a pending value, let the main loop act on it.
struct Body {
    virtual ~Body() = default;
    virtual void express(Emotion e) = 0;
    virtual void chirp(Chirp c) = 0;
    virtual void led(uint8_t r, uint8_t g, uint8_t b) = 0;
    virtual void say(const char* text) = 0;      // <= 95 chars, already truncated
    virtual void hud(const HudUpdate& u) = 0;
    virtual void scene(const char* name) = 0;    // <= 15 chars
    virtual Senses senses() = 0;
};
// The one dispatcher HTTP and the link both go through, so a command means the same thing on
// either transport. cmd is "express"|"chirp"|"led"|"say"|"hud"|"scene"|"ping"; obj is the JSON
// object holding the args (the request body over HTTP, the whole frame object over the link,
// whose own "cmd" key is ignored here). Returns the HTTP status and writes the JSON reply
// ({"ok":true}, {"pong":true}, or {"error":"..."}) into reply.
int apply_command(const char* cmd, const char* obj, size_t len, Body& body, char* reply, size_t cap);
// The /senses object, also what the link wraps in {"senses":...}. Returns bytes, 0 if it did not fit.
size_t senses_json(const Senses& s, char* out, size_t cap);
// Routes one parsed request per PROTOCOL.md v1 and writes the full HTTP response. Returns bytes.
size_t handle_request(const http::Request& req, Body& body, char* out, size_t cap);
// {"event":"<name>"} for POST <brain>/event and for the link. Returns bytes.
size_t event_json(const char* event, char* out, size_t cap);
}
