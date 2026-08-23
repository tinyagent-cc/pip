#pragma once
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace pip::brain {
using json = nlohmann::json;

// The Jetson's ears and eyes. `fail` turns it into a service that is up but
// broken (500); tests that want it plain unreachable point at 127.0.0.1:1.
class FakeCortex {
public:
    std::atomic<bool> fail{false};
    std::atomic<int> delay_ms{0};
    std::string heard = "what do you see";
    std::string lang = "en";
    std::string seen = "a desk with a keyboard";

    FakeCortex() {
        svr_.Post("/listen", [this](const httplib::Request& rq, httplib::Response& rs) {
            record("listen:" + rq.body);
            if (!reply_ok(rs)) return;
            rs.set_content(json{{"text", heard}, {"lang", lang}, {"ms", 1200}}.dump(), "application/json");
        });
        svr_.Post("/see", [this](const httplib::Request& rq, httplib::Response& rs) {
            auto j = json::parse(rq.body, nullptr, false);
            record("see:" + (j.is_object() ? j.value("question", std::string()) : std::string()));
            if (!reply_ok(rs)) return;
            rs.set_content(json{{"text", seen}, {"ms", 900}}.dump(), "application/json");
        });
        svr_.Post("/search", [this](const httplib::Request& rq, httplib::Response& rs) {
            auto j = json::parse(rq.body, nullptr, false);
            record("search:" + (j.is_object() ? j.value("query", std::string()) : std::string()));
            if (!reply_ok(rs)) return;
            rs.set_content(json{{"results", json::array({
                json{{"title", "Hit one"}, {"snippet", "first snippet"}, {"url", "https://a"}},
                json{{"title", "Hit two"}, {"snippet", "second snippet"}, {"url", "https://b"}},
            })}, {"ms", 800}}.dump(), "application/json");
        });
        svr_.Get("/health", [this](const httplib::Request&, httplib::Response& rs) {
            if (fail.load()) { rs.status = 500; return; }
            rs.set_content(R"({"ok":true})", "application/json");
        });
        start();
    }
    ~FakeCortex() { svr_.stop(); th_.join(); }
    std::string url() const { return "http://127.0.0.1:" + std::to_string(port_); }
    std::vector<std::string> snapshot() const { std::lock_guard<std::mutex> g(m_); return calls_; }

private:
    bool reply_ok(httplib::Response& rs) {
        if (int d = delay_ms.load(); d > 0) std::this_thread::sleep_for(std::chrono::milliseconds(d));
        if (!fail.load()) return true;
        rs.status = 500;
        rs.set_content(R"({"error":"cortex is having a moment"})", "application/json");
        return false;
    }
    void record(std::string s) { std::lock_guard<std::mutex> g(m_); calls_.push_back(std::move(s)); }
    void start() {
        port_ = svr_.bind_to_any_port("127.0.0.1");
        if (port_ <= 0) throw std::runtime_error("FakeCortex: bind_to_any_port failed");
        th_ = std::thread([this] { svr_.listen_after_bind(); });
        svr_.wait_until_ready();
    }
    mutable std::mutex m_;
    std::vector<std::string> calls_;
    httplib::Server svr_;
    int port_ = 0;
    std::thread th_;
};

// The Pi 5's voice. 16 samples per character of text, so a test can assert
// sizes without caring what the audio sounds like.
class FakeVoice {
public:
    std::atomic<bool> fail{false};
    std::atomic<int> delay_ms{0};

    FakeVoice() {
        svr_.Post("/tts", [this](const httplib::Request& rq, httplib::Response& rs) {
            auto j = json::parse(rq.body, nullptr, false);
            std::string text = j.is_object() ? j.value("text", std::string()) : std::string();
            record("tts:" + text);
            if (int d = delay_ms.load(); d > 0) std::this_thread::sleep_for(std::chrono::milliseconds(d));
            if (fail.load()) { rs.status = 500; rs.set_content(R"({"error":"no voice"})", "application/json"); return; }
            size_t n = text.size() * 16;
            std::string pcm(n * 2, '\0');
            for (size_t i = 0; i < n; ++i) {
                auto v = static_cast<int16_t>(8000 * std::sin(2.0 * 3.14159265 * 440.0 * static_cast<double>(i) / 16000.0));
                auto u = static_cast<uint16_t>(v);
                pcm[2 * i] = static_cast<char>(u & 0xFF);
                pcm[2 * i + 1] = static_cast<char>((u >> 8) & 0xFF);
            }
            rs.set_content(pcm, "application/octet-stream");
        });
        svr_.Get("/health", [this](const httplib::Request&, httplib::Response& rs) {
            if (fail.load()) { rs.status = 500; return; }
            rs.set_content(R"({"ok":true})", "application/json");
        });
        port_ = svr_.bind_to_any_port("127.0.0.1");
        if (port_ <= 0) throw std::runtime_error("FakeVoice: bind_to_any_port failed");
        th_ = std::thread([this] { svr_.listen_after_bind(); });
        svr_.wait_until_ready();
    }
    ~FakeVoice() { svr_.stop(); th_.join(); }
    std::string url() const { return "http://127.0.0.1:" + std::to_string(port_); }
    std::vector<std::string> snapshot() const { std::lock_guard<std::mutex> g(m_); return calls_; }

private:
    void record(std::string s) { std::lock_guard<std::mutex> g(m_); calls_.push_back(std::move(s)); }
    mutable std::mutex m_;
    std::vector<std::string> calls_;
    httplib::Server svr_;
    int port_ = 0;
    std::thread th_;
};

}  // namespace pip::brain
