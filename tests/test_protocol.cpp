#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include "check.h"
#include "pip/protocol.hpp"
#include "pip/http.hpp"
using namespace pip;
struct Fake : Body {
    Emotion e = Emotion::Idle; Chirp c = Chirp::Rise; int r = -1, g = -1, b = -1; int n_express = 0, n_chirp = 0, n_led = 0;
    char said[128] = {0}; char scene_name[32] = {0}; HudUpdate hud_u{}; int n_say = 0, n_hud = 0, n_scene = 0;
    Senses s{123.4f, 25.5f, true, {}, {}};
    void express(Emotion x) override { e = x; ++n_express; }
    void chirp(Chirp x) override { c = x; ++n_chirp; }
    void led(uint8_t rr, uint8_t gg, uint8_t bb) override { r = rr; g = gg; b = bb; ++n_led; }
    void say(const char* t) override { std::snprintf(said, sizeof said, "%s", t); ++n_say; }
    void hud(const HudUpdate& u) override { hud_u = u; ++n_hud; }
    void scene(const char* n) override { std::snprintf(scene_name, sizeof scene_name, "%s", n); ++n_scene; }
    Senses senses() override { return s; }
};
static std::string call(Fake& f, const char* raw) {
    http::Request req{}; CHECK(http::parse_request(raw, std::strlen(raw), req) == http::Parse::Complete);
    char out[512]; size_t n = handle_request(req, f, out, sizeof out); CHECK(n > 0); return std::string(out, n);
}
static void run() {
    Fake f;
    std::string r = call(f, "POST /express HTTP/1.0\r\nContent-Length: 19\r\n\r\n{\"emotion\":\"happy\"}");
    CHECK(r.rfind("HTTP/1.0 200", 0) == 0); CHECK(r.find("{\"ok\":true}") != std::string::npos);
    CHECK(f.e == Emotion::Happy); CHECK_EQ(f.n_express, 1);
    r = call(f, "POST /express HTTP/1.0\r\nContent-Length: 19\r\n\r\n{\"emotion\":\"angry\"}");
    CHECK(r.rfind("HTTP/1.0 400", 0) == 0); CHECK(r.find("{\"error\":\"unknown emotion\"}") != std::string::npos); CHECK_EQ(f.n_express, 1);
    r = call(f, "POST /express HTTP/1.0\r\nContent-Length: 2\r\n\r\n{}");
    CHECK(r.rfind("HTTP/1.0 400", 0) == 0); CHECK(r.find("missing emotion") != std::string::npos);
    r = call(f, "POST /chirp HTTP/1.0\r\nContent-Length: 16\r\n\r\n{\"name\":\"trill\"}");
    CHECK(r.rfind("HTTP/1.0 200", 0) == 0); CHECK(f.c == Chirp::Trill);
    // Plan B adds two chirps; /chirp has to accept them by name over both transports.
    r = call(f, "POST /chirp HTTP/1.0\r\nContent-Length: 15\r\n\r\n{\"name\":\"boot\"}");
    CHECK(r.rfind("HTTP/1.0 200", 0) == 0); CHECK(f.c == Chirp::Boot);
    r = call(f, "POST /chirp HTTP/1.0\r\nContent-Length: 14\r\n\r\n{\"name\":\"sad\"}");
    CHECK(r.rfind("HTTP/1.0 200", 0) == 0); CHECK(f.c == Chirp::Sad);
    r = call(f, "POST /led HTTP/1.0\r\nContent-Length: 22\r\n\r\n{\"r\":255,\"g\":0,\"b\":16}");
    CHECK(r.rfind("HTTP/1.0 200", 0) == 0); CHECK_EQ(f.r, 255); CHECK_EQ(f.g, 0); CHECK_EQ(f.b, 16);
    r = call(f, "POST /led HTTP/1.0\r\nContent-Length: 22\r\n\r\n{\"r\":256,\"g\":0,\"b\":16}");
    CHECK(r.rfind("HTTP/1.0 400", 0) == 0); CHECK_EQ(f.n_led, 1);
    r = call(f, "GET /senses HTTP/1.0\r\n\r\n");
    CHECK(r.rfind("HTTP/1.0 200", 0) == 0);
    CHECK(r.find("{\"light_lux\":123.4,\"temp_c\":25.5,\"button\":\"down\",") != std::string::npos);
    CHECK(r.find("X-Pip-Protocol: 1\r\n") != std::string::npos);
    CHECK(r.find("\"link\":{\"rx_frames\":0,\"rx_bad\":0,\"audio_dropped\":0}") != std::string::npos);
    CHECK(r.find("\"audio\":{\"free\":0,\"playing\":false}") != std::string::npos);
    r = call(f, "GET /express HTTP/1.0\r\n\r\n");
    CHECK(r.rfind("HTTP/1.0 405", 0) == 0);
    r = call(f, "GET /nope HTTP/1.0\r\n\r\n");
    CHECK(r.rfind("HTTP/1.0 404", 0) == 0);
    char ev[64]; size_t n = event_json("button.press", ev, sizeof ev); ev[n] = 0;
    CHECK_STREQ(ev, "{\"event\":\"button.press\"}");
    CHECK_STREQ(emotion_name(Emotion::Wink), "wink"); CHECK_STREQ(chirp_name(Chirp::Purr), "purr");
    Emotion e; CHECK(emotion_from("sleepy", e)); CHECK(e == Emotion::Sleepy); CHECK(!emotion_from("", e));

    // Sensor glitch: NaN lux must not produce malformed JSON, and must not leak into the
    // response as "nan" -- sanitized to -1.0 (an out-of-range sentinel, not a fabricated reading).
    f.s = Senses{NAN, 25.5f, true, {}, {}};
    r = call(f, "GET /senses HTTP/1.0\r\n\r\n");
    CHECK(r.rfind("HTTP/1.0 200", 0) == 0);
    CHECK(r.find("\"light_lux\":-1.0") != std::string::npos);

    // Out-of-physical-range readings clamp instead of overflowing the format buffer.
    f.s = Senses{3.4e38f, -3.4e38f, true, {}, {}};
    r = call(f, "GET /senses HTTP/1.0\r\n\r\n");
    CHECK(r.rfind("HTTP/1.0 200", 0) == 0);
    CHECK(r.find("\"light_lux\":200000.0,\"temp_c\":-100.0") != std::string::npos);
    CHECK(r.size() > 0 && r.back() == '}');

    CHECK_STREQ(http::reason(500), "Internal Server Error");

    // --- v1 ---
    Fake v;
    v.s = Senses{123.4f, 25.5f, true, {3, 1, 2}, {4096, true}};
    r = call(v, "GET /senses HTTP/1.0\r\n\r\n");
    CHECK(r.find("\"link\":{\"rx_frames\":3,\"rx_bad\":1,\"audio_dropped\":2}") != std::string::npos);
    CHECK(r.find("\"audio\":{\"free\":4096,\"playing\":true}") != std::string::npos);

    // The four emotions v1 adds are routable, not just enum values.
    for (const char* n : {"surprised", "sad", "listening", "talking"}) {
        Emotion ee; CHECK(emotion_from(n, ee));
        char req[128]; std::snprintf(req, sizeof req, "POST /express HTTP/1.0\r\nContent-Length: %u\r\n\r\n{\"emotion\":\"%s\"}",
                                     (unsigned)(14 + std::strlen(n)), n);
        r = call(v, req);
        CHECK(r.rfind("HTTP/1.0 200", 0) == 0); CHECK(v.e == ee);
    }
    CHECK_STREQ(emotion_name(Emotion::Surprised), "surprised");
    CHECK_STREQ(emotion_name(Emotion::Talking), "talking");
    CHECK_EQ((int)Emotion::Count, 10);

    r = call(v, "POST /say HTTP/1.0\r\nContent-Length: 26\r\n\r\n{\"text\":\"hello from wire\"}");
    CHECK(r.rfind("HTTP/1.0 200", 0) == 0); CHECK_STREQ(v.said, "hello from wire"); CHECK_EQ(v.n_say, 1);
    r = call(v, "POST /say HTTP/1.0\r\nContent-Length: 2\r\n\r\n{}");
    CHECK(r.rfind("HTTP/1.0 400", 0) == 0); CHECK_EQ(v.n_say, 1);
    r = call(v, "POST /say HTTP/1.0\r\nContent-Length: 11\r\n\r\n{\"text\":\"\"}");
    CHECK(r.rfind("HTTP/1.0 400", 0) == 0); CHECK_EQ(v.n_say, 1);
    {   // over-long text is truncated to 95 chars, not rejected
        std::string body = "{\"text\":\"" + std::string(140, 'x') + "\"}";
        char req[256]; std::snprintf(req, sizeof req, "POST /say HTTP/1.0\r\nContent-Length: %u\r\n\r\n%s", (unsigned)body.size(), body.c_str());
        r = call(v, req);
        CHECK(r.rfind("HTTP/1.0 200", 0) == 0); CHECK_EQ((int)std::strlen(v.said), 95);
    }

    r = call(v, "POST /scene HTTP/1.0\r\nContent-Length: 17\r\n\r\n{\"name\":\"reflex\"}");
    CHECK(r.rfind("HTTP/1.0 200", 0) == 0); CHECK_STREQ(v.scene_name, "reflex"); CHECK_EQ(v.n_scene, 1);
    r = call(v, "POST /scene HTTP/1.0\r\nContent-Length: 32\r\n\r\n{\"name\":\"waaaaay too long name\"}");
    CHECK(r.rfind("HTTP/1.0 400", 0) == 0); CHECK_EQ(v.n_scene, 1);

    r = call(v, "GET /ping HTTP/1.0\r\n\r\n");
    CHECK(r.rfind("HTTP/1.0 200", 0) == 0); CHECK(r.find("{\"pong\":true}") != std::string::npos);

    r = call(v, "POST /hud HTTP/1.0\r\nContent-Length: 54\r\n\r\n{\"reflex_us\":95,\"brain\":true,\"mind\":\"J\",\"scene\":\"who\"}");
    CHECK(r.rfind("HTTP/1.0 200", 0) == 0); CHECK_EQ(v.n_hud, 1);
    CHECK(v.hud_u.has_reflex_us && v.hud_u.reflex_us == 95);
    CHECK(v.hud_u.has_brain && v.hud_u.brain);
    CHECK(v.hud_u.has_mind && v.hud_u.mind == 'J');
    CHECK(v.hud_u.has_scene); CHECK_STREQ(v.hud_u.scene, "who");
    CHECK(!v.hud_u.has_judge_ms); CHECK(!v.hud_u.has_cortex);
    r = call(v, "POST /hud HTTP/1.0\r\nContent-Length: 16\r\n\r\n{\"reflex_us\":-4}");
    CHECK(r.rfind("HTTP/1.0 400", 0) == 0); CHECK_EQ(v.n_hud, 1);

    // apply_command is the same door the link uses; the frame's own "cmd" key is ignored.
    char reply[128];
    const char* obj = "{\"cmd\":\"hud\",\"judge_ms\":5800}";
    CHECK_EQ(apply_command("hud", obj, std::strlen(obj), v, reply, sizeof reply), 200);
    CHECK_STREQ(reply, "{\"ok\":true}");
    CHECK(v.hud_u.has_judge_ms && v.hud_u.judge_ms == 5800);
    CHECK(!v.hud_u.has_reflex_us && !v.hud_u.has_brain && !v.hud_u.has_cortex && !v.hud_u.has_mind && !v.hud_u.has_scene);
    const char* png = "{\"cmd\":\"ping\"}";
    CHECK_EQ(apply_command("ping", png, std::strlen(png), v, reply, sizeof reply), 200);
    CHECK_STREQ(reply, "{\"pong\":true}");
    CHECK_EQ(apply_command("nope", png, std::strlen(png), v, reply, sizeof reply), 404);

    // senses_json standalone, the shape the link wraps in {"senses":...}
    char sj[256];
    size_t sn = senses_json(v.s, sj, sizeof sj);
    CHECK(sn > 0 && sj[0] == '{' && sj[sn - 1] == '}');
    CHECK(std::string(sj).find("\"button\":\"down\"") != std::string::npos);
}
TEST_MAIN()
