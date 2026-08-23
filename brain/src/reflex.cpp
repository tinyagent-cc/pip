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
    // Rules first: alpha memories created by add_rule() are not back-filled
    // from WMEs asserted earlier, so room_ must be asserted after
    // install_rules() or the light-state rules never see it.
    install_rules();
    room_ = rx_.engine().assert_fact(std::string("room"), std::string("light"), std::string("high"));
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

// Guardrail rules match on the "call-N" facts tool_call_facts() asserts for
// the model's tool calls (post-response, pre-dispatch). They run through
// rx_.outcome() the same way reflex rules run through body_ calls; the
// middleware applies whatever vetoes/replace_args they queued once run()
// returns.
void Reflex::install_guardrails() {
    auto& eng = rx_.engine();

    // night-led-cap: cap any LED channel over the night cap. policy_.clamp()
    // already folds in the night flag, so a day-time call clamps to the same
    // value and no replace_arg is queued.
    eng.add_rule("night-led-cap").when(std::string("?c"), std::string("tool"), std::string("led"))
        .then([this](rete::ReteEngine& engine, const rete::Bindings& b) {
            Timer t;
            std::string id = std::get<std::string>(b.at("?c"));
            auto idx_wmes = engine.query(rete::Value(id), rete::Value(std::string("index")), std::nullopt);
            if (idx_wmes.empty()) return;
            auto* idx_p = std::get_if<int64_t>(&idx_wmes[0]->value);
            if (!idx_p) { log_.note("night-led-cap: non-integer index fact for " + id); return; }
            int idx = static_cast<int>(*idx_p);

            std::string origs, caps;
            for (const char* k : {"r", "g", "b"}) {
                auto arg_wmes = engine.query(rete::Value(id), rete::Value(std::string("arg:") + k), std::nullopt);
                if (arg_wmes.empty()) continue;
                int v = 0;
                if (auto* iv = std::get_if<int64_t>(&arg_wmes[0]->value)) v = static_cast<int>(*iv);
                else if (auto* dv = std::get_if<double>(&arg_wmes[0]->value)) v = static_cast<int>(*dv);
                int capped = policy_.clamp(v);
                if (!origs.empty()) { origs += ","; caps += ","; }
                origs += std::to_string(v); caps += std::to_string(capped);
                if (capped != v) rx_.outcome().replace_arg(idx, k, capped);
            }
            log_.reflex("night-led-cap", t.us(), origs.empty() ? "-" : origs + " -> " + caps);
        }).build();

    // chirp-rate: only the earliest tool call (lowest index) in a batch is
    // checked against the real rate limit; any other chirp call in the same
    // batch is vetoed outright. The engine does not fire matches in
    // assertion order for a single-condition rule with more than one match
    // (verified empirically), so the decision has to key off the call's
    // index rather than which fires first.
    eng.add_rule("chirp-rate").when(std::string("?c"), std::string("tool"), std::string("chirp"))
        .then([this](rete::ReteEngine& engine, const rete::Bindings& b) {
            Timer t;
            std::string id = std::get<std::string>(b.at("?c"));
            auto idx_wmes = engine.query(rete::Value(id), rete::Value(std::string("index")), std::nullopt);
            if (idx_wmes.empty()) return;
            auto* idx_p = std::get_if<int64_t>(&idx_wmes[0]->value);
            if (!idx_p) { log_.note("chirp-rate: non-integer index fact for " + id); return; }
            int idx = static_cast<int>(*idx_p);

            auto chirp_wmes = engine.query(std::nullopt, rete::Value(std::string("tool")), rete::Value(std::string("chirp")));
            int min_idx = idx;
            for (auto& w : chirp_wmes) {
                auto other_idx = engine.query(w->identifier, rete::Value(std::string("index")), std::nullopt);
                if (other_idx.empty()) continue;
                if (auto* iv = std::get_if<int64_t>(&other_idx[0]->value))
                    min_idx = std::min(min_idx, static_cast<int>(*iv));
            }

            if (idx != min_idx) {
                rx_.outcome().veto(idx, "chirp rate limit");
                log_.reflex("chirp-rate", t.us(), "vetoed idx=" + std::to_string(idx) + " (batched)");
            } else if (!policy_.chirp_allowed(now_ms())) {
                rx_.outcome().veto(idx, "chirp rate limit");
                log_.reflex("chirp-rate", t.us(), "vetoed idx=" + std::to_string(idx));
            } else {
                log_.reflex("chirp-rate", t.us(), "allowed idx=" + std::to_string(idx));
            }
        }).build();
}

tiny_agent::MiddlewareFn Reflex::guardrail_middleware() {
    middleware::ReflexConfig rc;
    rc.extract_response_facts = middleware::tool_call_facts;
    return rx_.middleware(rc);
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
