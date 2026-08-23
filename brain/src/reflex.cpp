#include "reflex.hpp"
#include <chrono>
#include <exception>
namespace pip::brain {
using namespace tiny_agent;
namespace {
struct Timer { std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    int64_t us() const { return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count(); } };
}

Reflex::Reflex(IBody& body, Policy& policy, EventLog& log) : body_(body), policy_(policy), log_(log) {
    room_ = rx_.engine().assert_fact(std::string("room"), std::string("light"), std::string("high"));
    install_rules();
    install_guardrails();
}

// Each rule: a named action that does its body calls, then logs rule + microseconds + what it did.
void Reflex::install_rules() {
    auto& eng = rx_.engine();
    auto act = [this](const char* rule, std::function<std::string()> run) {
        return [this, rule, run](rete::ReteEngine&, const rete::Bindings&) {
            Timer t; std::string detail = run(); ++fired_;
            log_.reflex(rule, t.us(), detail.empty() ? "-" : detail);
        };
    };
    eng.add_rule("press-wink").when(std::string("ev"), std::string("name"), std::string("button.press"))
        .then(act("press-wink", [this] {
            std::string d = body_.express("wink") ? "express=wink" : "express=wink(FAILED)";
            if (policy_.chirp_allowed(now_ms_)) d += body_.chirp("rise") ? " chirp=rise" : " chirp=rise(FAILED)";
            return d; })).build();
    eng.add_rule("hold-think").when(std::string("ev"), std::string("name"), std::string("button.hold"))
        .then(act("hold-think", [this] { body_.express("thinking"); return "express=thinking"; })).build();
    eng.add_rule("release-noop").when(std::string("ev"), std::string("name"), std::string("button.release"))
        .then(act("release-noop", [] { return std::string(); })).build();
    eng.add_rule("dark-sleepy").when(std::string("ev"), std::string("name"), std::string("light.low"))
        .then(act("dark-sleepy", [this] {
            rx_.engine().modify_fact(room_, std::string("room"), std::string("light"), std::string("low"));
            dark_ = true; policy_.night = true;
            body_.express("sleepy"); return "express=sleepy night=on"; })).build();
    eng.add_rule("bright-alert").salience(10)
        .when(std::string("ev"), std::string("name"), std::string("light.high"))
        .when(std::string("room"), std::string("light"), std::string("low"))
        .then(act("bright-alert", [this] {
            std::string d = "express=alert"; body_.express("alert");
            if (policy_.chirp_allowed(now_ms_)) { body_.chirp("trill"); d += " chirp=trill"; }
            return d; })).build();
    eng.add_rule("bright-note").salience(0).when(std::string("ev"), std::string("name"), std::string("light.high"))
        .then(act("bright-note", [this] {
            rx_.engine().modify_fact(room_, std::string("room"), std::string("light"), std::string("high"));
            dark_ = false; policy_.night = false; return "night=off"; })).build();
    eng.add_rule("hot-alert").when(std::string("ev"), std::string("name"), std::string("temp.hot"))
        .then(act("hot-alert", [this] {
            body_.express("alert"); int r = policy_.clamp(255); body_.led(r, 0, 0);
            return "express=alert led=" + std::to_string(r) + ",0,0"; })).build();
}

void Reflex::install_guardrails() {
    // Task 4 installs the guardrail rules; nothing to register yet.
}

tiny_agent::MiddlewareFn Reflex::guardrail_middleware() {
    return rx_.middleware({.extract_response_facts = middleware::tool_call_facts});
}

int Reflex::on_event(const std::string& name, int64_t now_ms) {
    Timer t; now_ms_ = now_ms; fired_ = 0;
    auto& eng = rx_.engine();
    auto wme = eng.assert_fact(std::string("ev"), std::string("name"), name);
    // Retracts the transient "ev" fact on scope exit, whether run() returns
    // normally or a rule action throws (a body call hitting real hardware).
    // Without this, a thrown exception would leak the fact into working
    // memory forever, corrupting every later on_event call.
    struct RetractGuard {
        rete::ReteEngine& eng; rete::WmePtr wme;
        ~RetractGuard() { eng.retract_fact(wme); }
    };
    try {
        RetractGuard guard{eng, wme};
        eng.run(256);
    } catch (const std::exception& e) {
        // The guard above has already retracted; safe to log and move on.
        log_.note(std::string("reflex action threw: ") + e.what());
    }
    eng.clear_refraction();  // bounds refraction_set_ growth; fresh WmeIds per assert already prevent wrong re-matches
    log_.event(name, "fired=" + std::to_string(fired_) + " total_us=" + std::to_string(t.us()));
    return fired_;
}

bool Reflex::on_senses(const Senses& s, int64_t now_ms) {
    if (!s.ok) return false;
    bool hot = s.temp_c > policy_.hot_c;
    bool rising = hot && !hot_;
    hot_ = hot;
    if (rising) on_event("temp.hot", now_ms);
    return rising;
}
}
