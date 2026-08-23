#include "body.hpp"
#include <algorithm>
namespace pip::brain {
HttpBody::HttpBody(std::string base_url, int timeout_ms) : base_url_(std::move(base_url)), timeout_ms_(timeout_ms) {
    cli_ = std::make_unique<httplib::Client>(base_url_);
    cli_->set_connection_timeout(0, timeout_ms_ * 1000);
    cli_->set_read_timeout(0, timeout_ms_ * 1000);
    cli_->set_write_timeout(0, timeout_ms_ * 1000);
    cli_->set_keep_alive(false);   // the Pico's raw-lwIP server closes after each reply
}
bool HttpBody::post(const char* path, const json& body) {
    bool ok;
    {
        std::lock_guard<std::mutex> g(m_);
        auto res = cli_->Post(path, body.dump(), "application/json");
        ok = res && res->status == 200;
    }
    // alive_ is set outside the lock so alive() never contends with an
    // in-flight request; a stale read is worth more than a blocked HUD push.
    alive_.store(ok);
    return ok;
}
bool HttpBody::express(const std::string& e) { return post("/express", json{{"emotion", e}}); }
bool HttpBody::chirp(const std::string& n) { return post("/chirp", json{{"name", n}}); }
bool HttpBody::led(int r, int g, int b) { return post("/led", json{{"r", r}, {"g", g}, {"b", b}}); }
bool HttpBody::say(const std::string& text) {
    return post("/say", json{{"text", text.substr(0, std::min(text.size(), SAY_MAX))}});
}
bool HttpBody::hud(const HudFields& f) { return post("/hud", f.to_json()); }
bool HttpBody::scene(const std::string& name) { return post("/scene", json{{"name", name}}); }
// Audio lives only on the UART link: 16 kHz PCM through the Pico's HTTP
// server would outrun it. Returning false lets the Speaker log a miss rather
// than pretend the body spoke.
bool HttpBody::speak(const std::vector<int16_t>&) { return false; }
bool HttpBody::ping() {
    bool ok;
    {
        std::lock_guard<std::mutex> g(m_);
        auto res = cli_->Get("/ping");
        ok = res && res->status == 200;
    }
    alive_.store(ok);
    return ok;
}
bool HttpBody::alive() const { return alive_.load(); }
Senses HttpBody::senses() {
    Senses s;
    httplib::Result res = [this] { std::lock_guard<std::mutex> g(m_); return cli_->Get("/senses"); }();
    if (!res || res->status != 200) { alive_.store(false); return s; }
    alive_.store(true);
    auto j = json::parse(res->body, nullptr, false);
    if (!j.is_object()) return s;
    try {
        s.light_lux = j.value("light_lux", -1.0); s.temp_c = j.value("temp_c", 0.0);
        s.button_down = j.value("button", "up") == "down"; s.ok = true;
    } catch (const json::exception&) {
        return Senses{};
    }
    return s;
}
}
