#include <cstring>
#include <string>
#include "check.h"
#include "pip/protocol.hpp"
#include "pip/http.hpp"
using namespace pip;
struct Fake : Body {
    Emotion e = Emotion::Idle; Chirp c = Chirp::Rise; int r = -1, g = -1, b = -1; int n_express = 0, n_chirp = 0, n_led = 0;
    void express(Emotion x) override { e = x; ++n_express; }
    void chirp(Chirp x) override { c = x; ++n_chirp; }
    void led(uint8_t rr, uint8_t gg, uint8_t bb) override { r = rr; g = gg; b = bb; ++n_led; }
    Senses senses() override { return Senses{123.4f, 25.5f, true}; }
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
    r = call(f, "POST /led HTTP/1.0\r\nContent-Length: 22\r\n\r\n{\"r\":255,\"g\":0,\"b\":16}");
    CHECK(r.rfind("HTTP/1.0 200", 0) == 0); CHECK_EQ(f.r, 255); CHECK_EQ(f.g, 0); CHECK_EQ(f.b, 16);
    r = call(f, "POST /led HTTP/1.0\r\nContent-Length: 22\r\n\r\n{\"r\":256,\"g\":0,\"b\":16}");
    CHECK(r.rfind("HTTP/1.0 400", 0) == 0); CHECK_EQ(f.n_led, 1);
    r = call(f, "GET /senses HTTP/1.0\r\n\r\n");
    CHECK(r.rfind("HTTP/1.0 200", 0) == 0);
    CHECK(r.find("{\"light_lux\":123.4,\"temp_c\":25.5,\"button\":\"down\"}") != std::string::npos);
    CHECK(r.find("X-Pip-Protocol: 0\r\n") != std::string::npos);
    r = call(f, "GET /express HTTP/1.0\r\n\r\n");
    CHECK(r.rfind("HTTP/1.0 405", 0) == 0);
    r = call(f, "GET /nope HTTP/1.0\r\n\r\n");
    CHECK(r.rfind("HTTP/1.0 404", 0) == 0);
    char ev[64]; size_t n = event_json("button.press", ev, sizeof ev); ev[n] = 0;
    CHECK_STREQ(ev, "{\"event\":\"button.press\"}");
    CHECK_STREQ(emotion_name(Emotion::Wink), "wink"); CHECK_STREQ(chirp_name(Chirp::Purr), "purr");
    Emotion e; CHECK(emotion_from("sleepy", e)); CHECK(e == Emotion::Sleepy); CHECK(!emotion_from("", e));
}
TEST_MAIN()
