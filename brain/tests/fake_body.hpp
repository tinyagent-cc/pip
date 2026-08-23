#pragma once
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "body.hpp"

namespace pip::brain {
struct FakeBody : IBody {
    std::mutex m; std::vector<std::string> calls; Senses next_senses{10.0, 25.0, false, true}; bool fail = false;
    bool throw_on_express = false;
    bool express(const std::string& e) override {
        std::lock_guard<std::mutex> g(m);
        if (throw_on_express) throw std::runtime_error("boom");
        calls.push_back("express:" + e); return !fail; }
    bool chirp(const std::string& n) override { std::lock_guard<std::mutex> g(m); calls.push_back("chirp:" + n); return !fail; }
    bool led(int r, int gg, int b) override { std::lock_guard<std::mutex> g(m); calls.push_back("led:" + std::to_string(r) + "," + std::to_string(gg) + "," + std::to_string(b)); return !fail; }
    bool say(const std::string& t) override { std::lock_guard<std::mutex> g(m); calls.push_back("say:" + t.substr(0, std::min(t.size(), SAY_MAX))); return !fail; }
    bool hud(const HudFields& f) override { std::lock_guard<std::mutex> g(m); calls.push_back("hud:" + f.to_json().dump()); return !fail; }
    bool scene(const std::string& n) override { std::lock_guard<std::mutex> g(m); calls.push_back("scene:" + n); return !fail; }
    // Records the sample count, not the samples: every test so far cares about
    // how much audio reached the body, not what it sounded like.
    bool speak(const std::vector<int16_t>& pcm) override { std::lock_guard<std::mutex> g(m); calls.push_back("speak:" + std::to_string(pcm.size())); return !fail; }
    bool ping() override { std::lock_guard<std::mutex> g(m); calls.push_back("ping"); return !fail; }
    bool alive() const override { return !fail; }
    Senses senses() override { std::lock_guard<std::mutex> g(m); calls.push_back("senses"); Senses s = next_senses; if (fail) s.ok = false; return s; }
    std::vector<std::string> snapshot() { std::lock_guard<std::mutex> g(m); return calls; }
    // Takes the mutex so a test can rewrite the fixture's readings while the
    // brain's worker thread is concurrently calling senses() in the background.
    void set_senses(Senses s, bool fail_flag) { std::lock_guard<std::mutex> g(m); next_senses = s; fail = fail_flag; }
};

// A Pico stand-in speaking protocol v0 on 127.0.0.1.
class FakePip {
public:
    Senses senses{10.0, 25.0, false, true};
    // When non-empty, sent verbatim as the body of GET /senses instead of the
    // normal `senses` JSON encoding -- lets a test hand HttpBody a wrong-typed
    // or malformed payload.
    std::string senses_override_body;
    FakePip() {
        static const std::set<std::string> emotions{"idle","happy","sleepy","thinking","alert","wink",
                                                    "surprised","sad","listening","talking"};
        static const std::set<std::string> chirps{"rise","trill","drop","purr","boot","sad"};
        svr_.Post("/express", [this](const httplib::Request& rq, httplib::Response& rs) {
            auto j = nlohmann::json::parse(rq.body, nullptr, false);
            std::string e = j.is_object() ? j.value("emotion", "") : "";
            if (!emotions.count(e)) { rs.status = 400; rs.set_content(R"({"error":"bad emotion"})", "application/json"); return; }
            record("express:" + e); rs.set_content(R"({"ok":true})", "application/json");
        });
        svr_.Post("/chirp", [this](const httplib::Request& rq, httplib::Response& rs) {
            auto j = nlohmann::json::parse(rq.body, nullptr, false);
            std::string n = j.is_object() ? j.value("name", "") : "";
            if (!chirps.count(n)) { rs.status = 400; rs.set_content(R"({"error":"bad chirp"})", "application/json"); return; }
            record("chirp:" + n); rs.set_content(R"({"ok":true})", "application/json");
        });
        svr_.Post("/led", [this](const httplib::Request& rq, httplib::Response& rs) {
            auto j = nlohmann::json::parse(rq.body, nullptr, false);
            record("led:" + std::to_string(j.value("r", -1)) + "," + std::to_string(j.value("g", -1)) + "," + std::to_string(j.value("b", -1)));
            rs.set_content(R"({"ok":true})", "application/json");
        });
        svr_.Post("/say", [this](const httplib::Request& rq, httplib::Response& rs) {
            auto j = nlohmann::json::parse(rq.body, nullptr, false);
            record("say:" + (j.is_object() ? j.value("text", std::string()) : std::string()));
            rs.set_content(R"({"ok":true})", "application/json");
        });
        svr_.Post("/hud", [this](const httplib::Request& rq, httplib::Response& rs) {
            auto j = nlohmann::json::parse(rq.body, nullptr, false);
            record("hud:" + (j.is_object() ? j.dump() : std::string("?")));
            rs.set_content(R"({"ok":true})", "application/json");
        });
        svr_.Post("/scene", [this](const httplib::Request& rq, httplib::Response& rs) {
            auto j = nlohmann::json::parse(rq.body, nullptr, false);
            record("scene:" + (j.is_object() ? j.value("name", std::string()) : std::string()));
            rs.set_content(R"({"ok":true})", "application/json");
        });
        svr_.Get("/ping", [this](const httplib::Request&, httplib::Response& rs) {
            record("ping");
            rs.set_content(R"({"pong":true})", "application/json");
        });
        svr_.Get("/senses", [this](const httplib::Request&, httplib::Response& rs) {
            record("senses");
            if (!senses_override_body.empty()) { rs.set_content(senses_override_body, "application/json"); return; }
            nlohmann::json j{{"light_lux", senses.light_lux}, {"temp_c", senses.temp_c}, {"button", senses.button_down ? "down" : "up"}};
            rs.set_content(j.dump(), "application/json");
        });
        port_ = svr_.bind_to_any_port("127.0.0.1");
        th_ = std::thread([this] { svr_.listen_after_bind(); });
        svr_.wait_until_ready();
    }
    ~FakePip() { svr_.stop(); th_.join(); }
    std::string url() const { return "http://127.0.0.1:" + std::to_string(port_); }
    std::vector<std::string> snapshot() { std::lock_guard<std::mutex> g(m_); return calls; }
private:
    void record(std::string s) { std::lock_guard<std::mutex> g(m_); calls.push_back(std::move(s)); }
    std::mutex m_; std::vector<std::string> calls; httplib::Server svr_; int port_ = 0; std::thread th_;
};
}  // namespace pip::brain
