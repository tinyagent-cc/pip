#include "pip/protocol.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include "pip/json_mini.hpp"
namespace pip {
namespace {
const char* const kEmotions[] = {"idle", "happy", "sleepy", "thinking", "alert", "wink", "surprised", "sad", "listening", "talking"};
const char* const kChirps[] = {"rise", "trill", "drop", "purr", "boot", "sad"};
static_assert(sizeof(kEmotions) / sizeof(kEmotions[0]) == (size_t)Emotion::Count, "emotion names out of step with the enum");
static_assert(sizeof(kChirps) / sizeof(kChirps[0]) == (size_t)Chirp::Count, "chirp names out of step with the enum");
// The commands apply_command knows. handle_request needs the list to tell "wrong method on a
// real route" (405) from "no such route" (404) without running the command to find out.
const char* const kCommands[] = {"express", "chirp", "led", "say", "hud", "scene", "ping"};
template <size_t N>
bool lookup(const char* const (&names)[N], const char* s, uint8_t& idx) {
    for (size_t i = 0; i < N; ++i) if (std::strcmp(names[i], s) == 0) { idx = (uint8_t)i; return true; }
    return false;
}
bool known_command(const char* s) { uint8_t i; return lookup(kCommands, s, i); }
size_t respond(char* out, size_t cap, int status, const char* body) { return http::build_response(out, cap, status, body, nullptr); }
int fail(char* reply, size_t cap, int status, const char* msg) {
    std::snprintf(reply, cap, "{\"error\":\"%s\"}", msg);
    return status;
}
int ok(char* reply, size_t cap) { std::snprintf(reply, cap, "{\"ok\":true}"); return 200; }
constexpr size_t SAY_MAX = 95, SCENE_MAX = 15;
}
const char* emotion_name(Emotion e) { return kEmotions[(uint8_t)e]; }
bool emotion_from(const char* s, Emotion& out) { uint8_t i; if (!lookup(kEmotions, s, i)) return false; out = (Emotion)i; return true; }
const char* chirp_name(Chirp c) { return kChirps[(uint8_t)c]; }
bool chirp_from(const char* s, Chirp& out) { uint8_t i; if (!lookup(kChirps, s, i)) return false; out = (Chirp)i; return true; }

size_t senses_json(const Senses& s, char* out, size_t cap) {
    // A sensor glitch (NaN/Inf) or an out-of-physical-range reading must not produce
    // malformed or misleading JSON; sanitize and clamp before formatting.
    float lux = std::isfinite(s.light_lux) ? s.light_lux : -1.0f;
    float t = std::isfinite(s.temp_c) ? s.temp_c : 0.0f;
    if (lux < -1.0f) lux = -1.0f; else if (lux > 200000.0f) lux = 200000.0f;
    if (t < -100.0f) t = -100.0f; else if (t > 200.0f) t = 200.0f;
    int n = std::snprintf(out, cap,
        "{\"light_lux\":%.1f,\"temp_c\":%.1f,\"button\":\"%s\","
        "\"link\":{\"rx_frames\":%lu,\"rx_bad\":%lu,\"audio_dropped\":%lu},"
        "\"audio\":{\"free\":%lu,\"playing\":%s}}",
        (double)lux, (double)t, s.button_down ? "down" : "up",
        (unsigned long)s.link.rx_frames, (unsigned long)s.link.rx_bad, (unsigned long)s.link.audio_dropped,
        (unsigned long)s.audio.free_bytes, s.audio.playing ? "true" : "false");
    return (n < 0 || (size_t)n >= cap) ? 0 : (size_t)n;
}

int apply_command(const char* cmd, const char* obj, size_t len, Body& body, char* reply, size_t cap) {
    if (std::strcmp(cmd, "ping") == 0) { std::snprintf(reply, cap, "{\"pong\":true}"); return 200; }
    if (std::strcmp(cmd, "express") == 0) {
        char name[16];
        if (!json::get_string(obj, len, "emotion", name, sizeof name)) return fail(reply, cap, 400, "missing emotion");
        Emotion e;
        if (!emotion_from(name, e)) return fail(reply, cap, 400, "unknown emotion");
        body.express(e);
        return ok(reply, cap);
    }
    if (std::strcmp(cmd, "chirp") == 0) {
        char name[16];
        if (!json::get_string(obj, len, "name", name, sizeof name)) return fail(reply, cap, 400, "missing name");
        Chirp c;
        if (!chirp_from(name, c)) return fail(reply, cap, 400, "unknown chirp");
        body.chirp(c);
        return ok(reply, cap);
    }
    if (std::strcmp(cmd, "led") == 0) {
        long r, g, b;
        if (!json::get_int(obj, len, "r", &r) || !json::get_int(obj, len, "g", &g) || !json::get_int(obj, len, "b", &b))
            return fail(reply, cap, 400, "need r,g,b");
        if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) return fail(reply, cap, 400, "r,g,b must be 0-255");
        body.led((uint8_t)r, (uint8_t)g, (uint8_t)b);
        return ok(reply, cap);
    }
    if (std::strcmp(cmd, "say") == 0) {
        // A long line is shortened, not refused: the brain writes sentences, and the screen
        // holds two lines of 24 characters.
        char text[SAY_MAX + 1];
        if (!json::get_string_trunc(obj, len, "text", text, sizeof text)) return fail(reply, cap, 400, "missing text");
        if (text[0] == '\0') return fail(reply, cap, 400, "empty text");
        body.say(text);
        return ok(reply, cap);
    }
    if (std::strcmp(cmd, "scene") == 0) {
        char name[SCENE_MAX + 1];
        if (!json::get_string(obj, len, "name", name, sizeof name)) return fail(reply, cap, 400, "missing or over-long name");
        body.scene(name);
        return ok(reply, cap);
    }
    if (std::strcmp(cmd, "hud") == 0) {
        HudUpdate u;
        long v;
        if (json::get_int(obj, len, "reflex_us", &v)) { if (v < 0) return fail(reply, cap, 400, "reflex_us must be >= 0"); u.has_reflex_us = true; u.reflex_us = v; }
        if (json::get_int(obj, len, "judge_ms", &v)) { if (v < 0) return fail(reply, cap, 400, "judge_ms must be >= 0"); u.has_judge_ms = true; u.judge_ms = v; }
        bool bv;
        if (json::get_bool(obj, len, "brain", &bv)) { u.has_brain = true; u.brain = bv; }
        if (json::get_bool(obj, len, "cortex", &bv)) { u.has_cortex = true; u.cortex = bv; }
        // Read into a roomy buffer: through get_string an over-long value is indistinguishable
        // from an absent one, and silently dropping a field the brain sent is worse than a 400.
        char s[64];
        if (json::get_string(obj, len, "mind", s, sizeof s)) {
            if (s[0] == '\0' || s[1] != '\0' || (s[0] != 'J' && s[0] != '5' && s[0] != '-')) return fail(reply, cap, 400, "mind must be J, 5 or -");
            u.has_mind = true; u.mind = s[0];
        }
        if (json::get_string(obj, len, "scene", s, sizeof s)) {
            if (std::strlen(s) > SCENE_MAX) return fail(reply, cap, 400, "scene too long");
            u.has_scene = true; std::snprintf(u.scene, sizeof u.scene, "%s", s);
        }
        body.hud(u);
        return ok(reply, cap);
    }
    return fail(reply, cap, 404, "not found");
}

size_t handle_request(const http::Request& req, Body& body, char* out, size_t cap) {
    bool is_post = std::strcmp(req.method, "POST") == 0;
    bool is_get = std::strcmp(req.method, "GET") == 0;
    const char* cmd = req.path[0] == '/' ? req.path + 1 : req.path;
    if (std::strcmp(cmd, "senses") == 0) {
        if (!is_get) return respond(out, cap, 405, "{\"error\":\"use GET\"}");
        char js[224];
        if (senses_json(body.senses(), js, sizeof js) == 0) return respond(out, cap, 500, "{\"error\":\"senses too large\"}");
        return respond(out, cap, 200, js);
    }
    if (!known_command(cmd)) return respond(out, cap, 404, "{\"error\":\"not found\"}");
    // /ping is a GET because it changes nothing; every other command is a POST.
    bool is_ping = std::strcmp(cmd, "ping") == 0;
    if (is_ping ? !is_get : !is_post) return respond(out, cap, 405, is_ping ? "{\"error\":\"use GET\"}" : "{\"error\":\"use POST\"}");
    char reply[128];
    int status = apply_command(cmd, req.body, req.body_len, body, reply, sizeof reply);
    return respond(out, cap, status, reply);
}
size_t event_json(const char* event, char* out, size_t cap) {
    int n = std::snprintf(out, cap, "{\"event\":\"%s\"}", event);
    return (n < 0 || (size_t)n >= cap) ? 0 : (size_t)n;
}
}
