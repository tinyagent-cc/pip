#pragma once
#include <tiny_agent/middleware/reflex.hpp>
#include <tiny_agent/tiny_agent.hpp>
#include <atomic>
#include <string>
#include "body.hpp"
#include "log.hpp"
#include "policy.hpp"

namespace pip::brain {
class Reflex {
public:
    Reflex(IBody& body, Policy& policy, EventLog& log);
    int on_event(const std::string& name, int64_t now_ms);
    // Wall time of the last on_event call, the number the HUD shows next to
    // the judgment's milliseconds.
    int64_t last_event_us() const { return last_us_; }
    bool on_senses(const Senses& s, int64_t now_ms);
    tiny_agent::MiddlewareFn guardrail_middleware();
    tiny_agent::middleware::ReflexEngine& rx() { return rx_; }
    bool room_dark() const { return dark_.load(); }
private:
    void install_rules();
    void install_guardrails();
    IBody& body_; Policy& policy_; EventLog& log_;
    tiny_agent::middleware::ReflexEngine rx_;
    rete::WmePtr room_;            // {"room","light","high"|"low"}
    std::atomic<bool> dark_{false};
    bool hot_ = false;
    int64_t now_ms_ = 0;           // set by on_event for the actions
    int64_t last_us_ = 0;          // duration of the last on_event, for the HUD
    int fired_ = 0;                // incremented by every action
};
}
