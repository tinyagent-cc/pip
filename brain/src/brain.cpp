#include "brain.hpp"
#include <algorithm>
#include <array>
#include <exception>

namespace pip::brain {
namespace {
bool is_known_event(const std::string& name) {
    static const std::array<const char*, 5> known{
        "button.press", "button.hold", "button.release", "light.low", "light.high"};
    return std::find(known.begin(), known.end(), name) != known.end();
}
}  // namespace

Brain::Brain(BrainConfig cfg, IBody& body, Policy& policy, EventLog& log, JudgmentConfig jcfg)
    : cfg_(cfg),
      body_(body),
      policy_(policy),
      log_(log),
      reflex_(body_, policy_, log_),
      judgment_(std::move(jcfg), body_, reflex_, log_),
      start_(std::chrono::steady_clock::now()),
      worker_(&Brain::worker_loop, this) {}

Brain::~Brain() {
    {
        std::lock_guard<std::mutex> g(m_);
        stop_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

bool Brain::post_event(const json& body) {
    if (!body.is_object() || !body.contains("event") || !body["event"].is_string()) {
        log_.note("post_event: malformed body");
        return false;
    }
    std::string name = body["event"].get<std::string>();
    if (!is_known_event(name)) {
        log_.note("post_event: unknown event " + name);
        return false;
    }
    {
        std::lock_guard<std::mutex> g(m_);
        queue_.push_back({name, now_ms()});
    }
    cv_.notify_all();
    return true;
}

void Brain::record_recent(const std::string& name, int64_t t_ms) {
    std::lock_guard<std::mutex> g(m_);
    recent_.emplace_back(name, t_ms);
    while (recent_.size() > cfg_.recent_events) recent_.pop_front();
}

void Brain::poll_senses() {
    Senses s = body_.senses();
    // A failed reading is never cached: the Pico may boot later, and a stale
    // "ok" flag under a plausible-looking light_lux/temp_c would otherwise
    // get told to the LLM as fact once the button.hold path stops re-probing.
    if (s.ok) {
        std::lock_guard<std::mutex> g(m_);
        last_senses_ = s;
        has_senses_ = true;
    }
    if (!s.ok) {
        if (!senses_fail_logged_) { log_.note("senses poll failed"); senses_fail_logged_ = true; }
    } else {
        senses_fail_logged_ = false;
    }
    reflex_.on_senses(s, now_ms());
}

void Brain::process_event(const std::string& name, int64_t t_ms) {
    // reflex_ shares Policy::last_chirp_ms_ with now_ms(); a press queued
    // behind a slow judgment call must be evaluated against the dequeue
    // clock, not the (possibly much older) enqueue time. t_ms still ages
    // record_recent entries, which want when the event actually happened.
    reflex_.on_event(name, now_ms());
    record_recent(name, t_ms);
    if (name == "button.hold" && judgment_.enabled()) {
        Context ctx;
        int64_t now = now_ms();
        bool have_senses;
        {
            std::lock_guard<std::mutex> g(m_);
            for (auto& [n, tms] : recent_) ctx.recent_events.push_back(std::to_string((now - tms) / 1000) + "s ago " + n);
            ctx.senses = last_senses_;
            have_senses = has_senses_;
        }
        // No cached reading yet, or the cached one was never ok (can't
        // happen once poll_senses only caches ok readings, but a fresh
        // reading here can still fail) -> probe once more before asking.
        if (!have_senses || !ctx.senses.ok) {
            Senses fresh = body_.senses();
            ctx.senses = fresh;
            if (fresh.ok) {
                std::lock_guard<std::mutex> g(m_);
                last_senses_ = fresh;
                has_senses_ = true;
            }
        }
        std::string reply = judgment_.react("button.hold", ctx);
        log_.note("judgment: " + reply);
    }
}

void Brain::worker_loop() {
    std::unique_lock<std::mutex> lock(m_);
    while (!stop_) {
        if (queue_.empty()) {
            busy_ = false;
            idle_cv_.notify_all();
            bool signaled = cv_.wait_for(lock, std::chrono::milliseconds(cfg_.senses_poll_ms),
                                          [this] { return stop_ || !queue_.empty(); });
            if (!signaled) {
                busy_ = true;
                lock.unlock();
                poll_senses();
                lock.lock();
                busy_ = false;
                idle_cv_.notify_all();
            }
            continue;
        }
        busy_ = true;
        QueuedEvent qe = std::move(queue_.front());
        queue_.pop_front();
        lock.unlock();
        try {
            process_event(qe.name, qe.t_ms);
        } catch (const std::exception& e) {
            log_.note(std::string("brain worker: ") + e.what());
        } catch (...) {
            log_.note("brain worker: unknown exception");
        }
        lock.lock();
    }
    busy_ = false;
    idle_cv_.notify_all();
}

void Brain::wait_idle() {
    std::unique_lock<std::mutex> lock(m_);
    // stop_ short-circuits the predicate so a waiter can never block forever
    // if the worker exits (destructor running) with a non-empty queue.
    idle_cv_.wait(lock, [this] { return stop_ || (queue_.empty() && !busy_); });
}

json Brain::health() const {
    std::lock_guard<std::mutex> g(m_);
    auto uptime_s = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start_).count();
    return json{
        {"ok", true},
        {"uptime_s", uptime_s},
        {"events", log_.count(EventLog::Kind::Event)},
        {"reflexes", log_.count(EventLog::Kind::Reflex)},
        {"llm_calls", log_.count(EventLog::Kind::Llm)},
        {"night", policy_.night.load()},
        {"llm", judgment_.enabled()},
        {"queue", queue_.size()},
    };
}

json Brain::log_tail(size_t n) const { return log_.tail(n); }

}  // namespace pip::brain
