#include "director.hpp"
#include <algorithm>
#include <chrono>
#include <exception>

namespace pip::brain {

void SteadyClock::sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
int64_t SteadyClock::now_ms() { return pip::brain::now_ms(); }

Director::Director(Brain& brain, IBody& body, EventLog& log, Clock& clock)
    : brain_(brain), body_(body), log_(log), clock_(clock) {}

Director::~Director() { wait(); }

std::vector<std::string> Director::names() {
    return {"reflex", "night", "fallback", "fever", "who", "tour"};
}

std::string Director::current() const {
    std::lock_guard<std::mutex> g(m_);
    return current_;
}

bool Director::run(const std::string& name) {
    auto all = names();
    if (std::find(all.begin(), all.end(), name) == all.end()) return false;
    std::lock_guard<std::mutex> g(m_);
    if (running_.load()) return false;
    if (th_.joinable()) th_.join();     // the previous scene has finished; reap it
    running_.store(true);
    current_ = name;
    th_ = std::thread(&Director::play, this, name);
    return true;
}

void Director::wait() {
    std::unique_lock<std::mutex> lock(m_);
    if (!th_.joinable()) return;
    std::thread t = std::move(th_);
    lock.unlock();                      // joining under the lock would deadlock run()
    t.join();
}

void Director::play(const std::string& name) {
    log_.note("scene start " + name);
    try {
        if (name == "reflex") scene_reflex();
        else if (name == "night") scene_night();
        else if (name == "fallback") scene_fallback();
        else if (name == "fever") scene_fever();
        else if (name == "who") scene_who();
        else if (name == "tour") scene_tour();
    } catch (const std::exception& e) {
        log_.note(std::string("scene ") + name + " threw: " + e.what());
    }
    caption("");
    body_.express("idle");
    log_.note("scene end " + name);
    {
        std::lock_guard<std::mutex> g(m_);
        current_.clear();
    }
    running_.store(false);
}

void Director::say(const std::string& line) {
    if (auto* sp = brain_.speaker()) sp->say(line);
    else body_.say(line);
}

void Director::caption(const std::string& name) { brain_.set_scene_name(name); }

bool Director::wait_for_event(const std::string& event, int ms) {
    uint64_t before = brain_.event_count(event);
    clock_.sleep_ms(ms);
    return brain_.event_count(event) > before;
}

// Press to wink is the whole point of the reflex layer, so the scene waits
// for a real press and only stages one if nobody obliges.
void Director::scene_reflex() {
    caption("reflex");
    say("Press my button.");
    if (!wait_for_event("button.press", 6000)) brain_.inject("button.press");
    say("That was a rule. Microseconds. Now hold me and ask something.");
    if (!wait_for_event("button.hold", 12000)) brain_.inject("button.hold");
    brain_.wait_idle();     // let the answer land before the scene bows out
}

void Director::scene_night() {
    caption("night");
    say("Cover my light sensor.");
    if (!wait_for_event("light.low", 8000)) brain_.inject("light.low");
    brain_.wait_idle();     // the sleepy face and the night flag come from that event
    say("Dark means sleepy, and my rules cap the LED.");
    int capped = brain_.led_capped(255, 0, 0);
    say("Rule capped the LED to " + std::to_string(capped) + ".");
    clock_.sleep_ms(5000);
    brain_.inject("light.high");
    brain_.wait_idle();
}

// The Jetson stays up; the judgment is simply told to ask the Pi 5 instead.
// Nobody has to power anything down on camera.
void Director::scene_fallback() {
    caption("fallback");
    brain_.warm_fallback();     // prime the Pi 5's prompt cache while the intro plays
    say("Cortex offline. The next answer comes from the Pi five.");
    brain_.set_force_fallback(true);
    say("Hold me and ask something.");
    for (int waited = 0; waited < 60000; waited += 2000) {
        if (wait_for_event("button.hold", 2000)) break;
    }
    brain_.wait_idle();
    brain_.set_force_fallback(false);
}

void Director::scene_fever() {
    caption("fever");
    say("Warm my chip.");
    if (!wait_for_event("temp.hot", 10000)) brain_.inject("temp.hot");
    brain_.wait_idle();
}

void Director::scene_who() {
    caption("who");
    say("Who's there?");
    say(brain_.look("Describe who or what is in front of the camera in one short sentence."));
}

// Unattended: every wait is staged, nothing depends on a hand at the desk.
// The beats are three seconds because a spoken sentence takes about that
// long, and a line that starts before the last one finishes reads as a
// glitch on film.
void Director::scene_tour() {
    caption("tour");
    for (const char* line : {"I'm Pip. My body is a Pico: face, chirps, senses.",
                             "My brain is a Pi Zero: rules in microseconds, an agent in seconds.",
                             "My cortex is a Jetson: ears, eyes, and the model that thinks.",
                             "If the cortex is gone, a Pi five answers instead, and it lends me its voice."}) {
        say(line);
        clock_.sleep_ms(3000);
    }
    clock_.sleep_ms(4000);
    scene_reflex();
    clock_.sleep_ms(4000);
    scene_night();
    clock_.sleep_ms(4000);
    scene_who();
    clock_.sleep_ms(4000);
    caption("tour");
    say("That's all of me. Press my button any time.");
    clock_.sleep_ms(3000);
}

}  // namespace pip::brain
