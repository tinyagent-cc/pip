#include "pip/protocol.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include "pip/json_mini.hpp"
namespace pip {
namespace {
const char* const kEmotions[] = {"idle", "happy", "sleepy", "thinking", "alert", "wink"};
const char* const kChirps[] = {"rise", "trill", "drop", "purr"};
template <size_t N>
bool lookup(const char* const (&names)[N], const char* s, uint8_t& idx) {
    for (size_t i = 0; i < N; ++i) if (std::strcmp(names[i], s) == 0) { idx = (uint8_t)i; return true; }
    return false;
}
size_t respond(char* out, size_t cap, int status, const char* body) { return http::build_response(out, cap, status, body, nullptr); }
}
const char* emotion_name(Emotion e) { return kEmotions[(uint8_t)e]; }
bool emotion_from(const char* s, Emotion& out) { uint8_t i; if (!lookup(kEmotions, s, i)) return false; out = (Emotion)i; return true; }
const char* chirp_name(Chirp c) { return kChirps[(uint8_t)c]; }
bool chirp_from(const char* s, Chirp& out) { uint8_t i; if (!lookup(kChirps, s, i)) return false; out = (Chirp)i; return true; }

size_t handle_request(const http::Request& req, Body& body, char* out, size_t cap) {
    bool is_post = std::strcmp(req.method, "POST") == 0;
    bool is_get = std::strcmp(req.method, "GET") == 0;
    if (std::strcmp(req.path, "/express") == 0) {
        if (!is_post) return respond(out, cap, 405, "{\"error\":\"use POST\"}");
        char name[16];
        if (!json::get_string(req.body, req.body_len, "emotion", name, sizeof name)) return respond(out, cap, 400, "{\"error\":\"missing emotion\"}");
        Emotion e;
        if (!emotion_from(name, e)) return respond(out, cap, 400, "{\"error\":\"unknown emotion\"}");
        body.express(e);
        return respond(out, cap, 200, "{\"ok\":true}");
    }
    if (std::strcmp(req.path, "/chirp") == 0) {
        if (!is_post) return respond(out, cap, 405, "{\"error\":\"use POST\"}");
        char name[16];
        if (!json::get_string(req.body, req.body_len, "name", name, sizeof name)) return respond(out, cap, 400, "{\"error\":\"missing name\"}");
        Chirp c;
        if (!chirp_from(name, c)) return respond(out, cap, 400, "{\"error\":\"unknown chirp\"}");
        body.chirp(c);
        return respond(out, cap, 200, "{\"ok\":true}");
    }
    if (std::strcmp(req.path, "/led") == 0) {
        if (!is_post) return respond(out, cap, 405, "{\"error\":\"use POST\"}");
        long r, g, b;
        if (!json::get_int(req.body, req.body_len, "r", &r) || !json::get_int(req.body, req.body_len, "g", &g) || !json::get_int(req.body, req.body_len, "b", &b))
            return respond(out, cap, 400, "{\"error\":\"need r,g,b\"}");
        if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) return respond(out, cap, 400, "{\"error\":\"r,g,b must be 0-255\"}");
        body.led((uint8_t)r, (uint8_t)g, (uint8_t)b);
        return respond(out, cap, 200, "{\"ok\":true}");
    }
    if (std::strcmp(req.path, "/senses") == 0) {
        if (!is_get) return respond(out, cap, 405, "{\"error\":\"use GET\"}");
        Senses s = body.senses();
        // A sensor glitch (NaN/Inf) or an out-of-physical-range reading must not produce
        // malformed or misleading JSON; sanitize and clamp before formatting.
        float lux = std::isfinite(s.light_lux) ? s.light_lux : -1.0f;
        float t = std::isfinite(s.temp_c) ? s.temp_c : 0.0f;
        if (lux < -1.0f) lux = -1.0f; else if (lux > 200000.0f) lux = 200000.0f;
        if (t < -100.0f) t = -100.0f; else if (t > 200.0f) t = 200.0f;
        char js[96];
        int n = std::snprintf(js, sizeof js, "{\"light_lux\":%.1f,\"temp_c\":%.1f,\"button\":\"%s\"}", (double)lux, (double)t, s.button_down ? "down" : "up");
        if (n < 0 || (size_t)n >= sizeof js) return respond(out, cap, 500, "{\"error\":\"senses too large\"}");
        return respond(out, cap, 200, js);
    }
    return respond(out, cap, 404, "{\"error\":\"not found\"}");
}
size_t event_json(const char* event, char* out, size_t cap) {
    int n = std::snprintf(out, cap, "{\"event\":\"%s\"}", event);
    return (n < 0 || (size_t)n >= cap) ? 0 : (size_t)n;
}
}
