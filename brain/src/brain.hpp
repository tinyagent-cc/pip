#pragma once
#include <nlohmann/json.hpp>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include "body.hpp"
#include "judgment.hpp"
#include "log.hpp"
#include "policy.hpp"
#include "reflex.hpp"

namespace pip::brain {
using json = nlohmann::json;

struct BrainConfig { int senses_poll_ms = 10000; size_t recent_events = 10; };

// Owns the worker thread that turns body-protocol events into reflex/judgment
// work off the HTTP request thread, plus a background senses poller. All
// public methods are safe to call concurrently (from HTTP handlers and from
// tests).
class Brain {
public:
    Brain(BrainConfig cfg, IBody& body, Policy& policy, EventLog& log, JudgmentConfig jcfg);
    ~Brain();

    // Validates {"event": <known name>}, enqueues it for the worker, returns
    // false (and logs a note) on anything else -- non-object JSON, missing
    // "event", or an unknown event name.
    bool post_event(const json& body);
    json health() const;
    json log_tail(size_t n) const;
    // Blocks until the queue is drained and the worker is parked back in its
    // condvar wait (i.e. any in-flight event, including a judgment.react()
    // call, has finished).
    void wait_idle();

private:
    struct QueuedEvent { std::string name; int64_t t_ms; };

    void worker_loop();
    void process_event(const std::string& name, int64_t t_ms);
    void poll_senses();
    void record_recent(const std::string& name, int64_t t_ms);

    BrainConfig cfg_;
    IBody& body_;
    Policy& policy_;
    EventLog& log_;
    Reflex reflex_;
    Judgment judgment_;
    std::chrono::steady_clock::time_point start_;

    mutable std::mutex m_;
    std::condition_variable cv_;       // queue not-empty, or stop
    std::condition_variable idle_cv_;  // signalled whenever the worker is idle
    std::deque<QueuedEvent> queue_;
    bool stop_ = false;
    bool busy_ = false;  // true while the worker is processing (event or senses poll)

    std::deque<std::pair<std::string, int64_t>> recent_;  // name, t_ms; oldest first, capped by cfg_.recent_events
    Senses last_senses_{};
    bool has_senses_ = false;
    bool senses_fail_logged_ = false;  // logs a poll failure on state change only, not every tick

    std::thread worker_;  // started last: must not touch members constructed after it
};
}  // namespace pip::brain
