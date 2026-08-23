#include "body.hpp"
namespace pip::brain {
HttpBody::HttpBody(std::string base_url, int timeout_ms) : base_url_(std::move(base_url)), timeout_ms_(timeout_ms) {
    cli_ = std::make_unique<httplib::Client>(base_url_);
    cli_->set_connection_timeout(0, timeout_ms_ * 1000);
    cli_->set_read_timeout(0, timeout_ms_ * 1000);
    cli_->set_write_timeout(0, timeout_ms_ * 1000);
    cli_->set_keep_alive(false);   // the Pico's raw-lwIP server closes after each reply
}
bool HttpBody::post(const char* path, const json& body) {
    std::lock_guard<std::mutex> g(m_);
    auto res = cli_->Post(path, body.dump(), "application/json");
    return res && res->status == 200;
}
bool HttpBody::express(const std::string& e) { return post("/express", json{{"emotion", e}}); }
bool HttpBody::chirp(const std::string& n) { return post("/chirp", json{{"name", n}}); }
bool HttpBody::led(int r, int g, int b) { return post("/led", json{{"r", r}, {"g", g}, {"b", b}}); }
Senses HttpBody::senses() {
    std::lock_guard<std::mutex> g(m_);
    Senses s;
    auto res = cli_->Get("/senses");
    if (!res || res->status != 200) return s;
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
