#include "brain.hpp"
#include <algorithm>
#include <array>
#include <exception>

namespace pip::brain {
namespace {
bool is_known_event(const std::string& name) {
    static const std::array<const char*, 6> known{
        "button.press", "button.hold", "button.release", "light.low", "light.high", "temp.hot"};
    return std::find(known.begin(), known.end(), name) != known.end();
}
}  // namespace

Brain::Brain(BrainConfig cfg, IBody& body, Policy& policy, EventLog& log, JudgmentConfig jcfg,
             Cortex* cortex, Voice* voice)
    : cfg_(cfg),
      body_(body),
      policy_(policy),
      log_(log),
      cortex_(cortex),
      voice_(voice),
      speaker_(voice ? std::make_unique<Speaker>(body, *voice, log) : nullptr),
      reflex_(body_, policy_, log_),
      judgment_(std::move(jcfg), body_, reflex_, log_, speaker_.get(), cortex_),
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
    count_event(name);
    {
        std::lock_guard<std::mutex> g(m_);
        queue_.push_back({name, now_ms(), false});
    }
    cv_.notify_all();
    return true;
}

void Brain::inject(const std::string& event) {
    if (!is_known_event(event)) { log_.note("inject: unknown event " + event); return; }
    log_.note("simulated event " + event);
    count_event(event);
    {
        std::lock_guard<std::mutex> g(m_);
        queue_.push_back({event, now_ms(), true});
    }
    cv_.notify_all();
}

void Brain::count_event(const std::string& name) {
    std::lock_guard<std::mutex> g(m_);
    ++counts_[name];
}

uint64_t Brain::event_count(const std::string& name) const {
    std::lock_guard<std::mutex> g(m_);
    auto it = counts_.find(name);
    return it == counts_.end() ? 0 : it->second;
}

void Brain::record_recent(const std::string& name, int64_t t_ms) {
    std::lock_guard<std::mutex> g(m_);
    recent_.emplace_back(name, t_ms);
    while (recent_.size() > cfg_.recent_events) recent_.pop_front();
}

void Brain::set_scene_name(const std::string& name) {
    {
        std::lock_guard<std::mutex> g(m_);
        scene_ = name;
    }
    // Both halves of the protocol: the scene command tells the body which
    // script is running, the HUD field puts the name on the strip.
    body_.scene(name);
    HudFields f;
    f.scene = name;
    body_.hud(f);
}

std::string Brain::scene_name() const {
    std::lock_guard<std::mutex> g(m_);
    return scene_;
}

std::string Brain::look(const std::string& question) {
    if (cortex_) {
        if (auto seen = cortex_->see(question)) return *seen;
    }
    return "I can't see right now";
}

int Brain::led_capped(int r, int g, int b) {
    int cr = policy_.clamp(r), cg = policy_.clamp(g), cb = policy_.clamp(b);
    body_.led(cr, cg, cb);
    return std::max({cr, cg, cb});
}

// Cheap health GETs, folded into the senses tick so nothing else has to own a
// timer. The HUD is only pushed when a boolean actually changed: the demo's
// service lights should blink on an outage, not on every poll.
void Brain::refresh_services() {
    bool c = cortex_ && cortex_->enabled() ? cortex_->ok() : false;
    bool v = voice_ && voice_->enabled() ? voice_->ok() : false;
    bool changed;
    {
        std::lock_guard<std::mutex> g(m_);
        changed = (c != cortex_ok_) || (v != voice_ok_);
        cortex_ok_ = c;
        voice_ok_ = v;
    }
    if (!changed) return;
    HudFields f;
    f.brain = true;
    f.cortex = c;
    body_.hud(f);
    log_.note(std::string("services: cortex=") + (c ? "up" : "down") + " voice=" + (v ? "up" : "down"));
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
    // The poller raises temp.hot inside the reflex engine, so it never passes
    // through post_event; count it here or the fever scene cannot tell that
    // the chip really did get warm.
    if (reflex_.on_senses(s, now_ms())) count_event("temp.hot");
    refresh_services();
}

void Brain::process_event(const std::string& name, int64_t t_ms) {
    // reflex_ shares Policy::last_chirp_ms_ with now_ms(); a press queued
    // behind a slow judgment call must be evaluated against the dequeue
    // clock, not the (possibly much older) enqueue time. t_ms still ages
    // record_recent entries, which want when the event actually happened.
    reflex_.on_event(name, now_ms());
    {
        HudFields f;
        f.reflex_us = static_cast<long>(reflex_.last_event_us());
        body_.hud(f);      // the microseconds column, pushed on every event
    }
    record_recent(name, t_ms);
    if (name != "button.hold" || !judgment_.enabled()) return;

    bool cortex_up = cortex_ && cortex_->enabled();
    std::string transcript, lang;
    if (cortex_up) {
        auto heard = cortex_->listen(cfg_.listen_seconds);
        cortex_up = heard.has_value();
        if (heard) { transcript = heard->text; lang = heard->lang; }
        else log_.note("cortex listen failed: " + cortex_->last_error());
        HudFields f;
        f.cortex = cortex_up;
        body_.hud(f);
        std::lock_guard<std::mutex> g(m_);
        cortex_ok_ = cortex_up;
    }

    Context ctx;
    ctx.transcript = transcript;
    ctx.lang = lang;
    ctx.dialogue.assign(dialogue_.begin(), dialogue_.end());
    int64_t now = now_ms();
    bool have_senses;
    {
        std::lock_guard<std::mutex> g(m_);
        for (auto& [n, tms] : recent_) ctx.recent_events.push_back(std::to_string((now - tms) / 1000) + "s ago " + n);
        ctx.senses = last_senses_;
        have_senses = has_senses_;
    }
    // No cached reading yet, or the cached one was never ok (can't happen
    // once poll_senses only caches ok readings, but a fresh reading here can
    // still fail) -> probe once more before asking.
    if (!have_senses || !ctx.senses.ok) {
        Senses fresh = body_.senses();
        ctx.senses = fresh;
        if (fresh.ok) {
            std::lock_guard<std::mutex> g(m_);
            last_senses_ = fresh;
            has_senses_ = true;
        }
    }

    body_.express("thinking");
    Verdict v = judgment_.react("button.hold", ctx);
    {
        std::lock_guard<std::mutex> g(m_);
        mind_ = v.mind;
        judge_ms_ = v.ms;
    }
    HudFields f;
    f.judge_ms = v.ms;
    f.mind = v.mind;
    f.cortex = cortex_up;
    body_.hud(f);
    log_.note("judgment: " + v.reply);
    // Pip remembers the exchange, so "what did I just ask you" works. Four
    // exchanges is plenty for a desk chat and keeps the prompt small.
    if (!transcript.empty() || !v.reply.empty()) {
        dialogue_.emplace_back(transcript, v.reply);
        while (dialogue_.size() > 4) dialogue_.pop_front();
    }
    // A drop chirp is how a mute Pip admits it: either there was no answer at
    // all, or the last thing it tried to say never made it to the speaker.
    // The speaker's flag is read, not waited on -- the worker must not block
    // on speech, so a voice that dies mid-demo is confessed on the next hold.
    if (v.reply.empty() || (speaker_ && !speaker_->last_voice_ok())) body_.chirp("drop");
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
    {
        std::unique_lock<std::mutex> lock(m_);
        // stop_ short-circuits the predicate so a waiter can never block
        // forever if the worker exits (destructor running) with a non-empty
        // queue.
        idle_cv_.wait(lock, [this] { return stop_ || (queue_.empty() && !busy_); });
    }
    if (speaker_) speaker_->wait_idle();
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
        {"link", body_.alive()},
        {"cortex", cortex_ok_},
        {"voice", voice_ok_},
        {"mind", std::string(1, mind_)},
        {"judge_ms", judge_ms_},
        {"scene", scene_},
    };
}

json Brain::log_tail(size_t n) const { return log_.tail(n); }

}  // namespace pip::brain
