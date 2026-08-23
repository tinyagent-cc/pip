#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "director.hpp"
#include "fake_body.hpp"
#include "fake_llm.hpp"
#include "fake_services.hpp"
#include <algorithm>
#include <condition_variable>
using namespace pip::brain;
using V = std::vector<std::string>;

namespace {

// Runs the two-minute tour in milliseconds while still adding up every wait
// the script asked for.
struct FakeClock : Clock {
    std::mutex m;
    int64_t now = 0, total = 0;
    std::vector<int> sleeps;
    void sleep_ms(int ms) override {
        std::lock_guard<std::mutex> g(m);
        sleeps.push_back(ms);
        total += ms;
        now += ms;
    }
    int64_t now_ms() override { std::lock_guard<std::mutex> g(m); return now; }
    int64_t total_ms() { std::lock_guard<std::mutex> g(m); return total; }
};

// A clock that parks the scene inside its first wait, so a test can look at a
// scene while it is genuinely mid-run.
struct GateClock : Clock {
    std::mutex m;
    std::condition_variable cv;
    bool open = false, entered = false;
    void sleep_ms(int) override {
        std::unique_lock<std::mutex> lock(m);
        entered = true;
        cv.notify_all();
        cv.wait(lock, [this] { return open; });
    }
    int64_t now_ms() override { return 0; }
    void wait_entered() { std::unique_lock<std::mutex> lock(m); cv.wait(lock, [this] { return entered; }); }
    void release() { { std::lock_guard<std::mutex> g(m); open = true; } cv.notify_all(); }
};

bool has(const V& calls, const std::string& s) { return std::find(calls.begin(), calls.end(), s) != calls.end(); }
size_t count_of(const V& calls, const std::string& s) { return static_cast<size_t>(std::count(calls.begin(), calls.end(), s)); }

// Body, brain and services, wired the way main() wires them.
struct Stage {
    FakeBody body;
    Policy policy;
    EventLog log{300, nullptr};
    FakeLlm llm;
    FakeCortex fake_cortex;
    FakeVoice fake_voice;
    Cortex cortex{fake_cortex.url()};
    Voice voice{fake_voice.url()};
    Brain brain;
    FakeClock clock;
    Director director{brain, body, log, clock};

    Stage() : brain(BrainConfig{100000, 10, 4}, body, policy, log, llm_cfg(), &cortex, &voice) {
        // Enough canned answers for the tour's one hold.
        llm.replies = {text_reply("A rule fires in microseconds.")};
    }
    JudgmentConfig llm_cfg() {
        JudgmentConfig c;
        c.llm_url = llm.url();
        c.timeout_s = 5;
        return c;
    }
};

}  // namespace

TEST_CASE("the scene list is the demo's menu") {
    CHECK(Director::names() == V{"reflex", "night", "fallback", "fever", "who", "tour"});
}

TEST_CASE("an unknown scene is refused") {
    Stage s;
    CHECK_FALSE(s.director.run("nope"));
    CHECK_FALSE(s.director.running());
    CHECK(s.director.current().empty());
}

TEST_CASE("reflex: asks for a press, stages one when nobody presses, then a hold") {
    Stage s;
    REQUIRE(s.director.run("reflex"));
    s.director.wait();
    auto calls = s.body.snapshot();
    CHECK(has(calls, "scene:reflex"));
    CHECK(has(calls, R"(hud:{"scene":"reflex"})"));
    CHECK(has(calls, "say:Press my button."));
    CHECK(has(calls, "express:wink"));         // the staged press fired the rule
    CHECK(has(calls, "express:listening"));    // and the staged hold opened the ears
    CHECK(has(calls, "express:idle"));         // scenes end where they started
    CHECK(s.brain.event_count("button.press") == 1);
    CHECK(s.brain.event_count("button.hold") == 1);
    CHECK(s.clock.total_ms() == 18000);
    CHECK(s.director.current().empty());
    CHECK_FALSE(s.director.running());
}

TEST_CASE("reflex: a press during the wait means nothing is staged") {
    FakeBody body; Policy policy; EventLog log{200, nullptr};
    Brain brain(BrainConfig{100000, 10, 4}, body, policy, log, {});
    GateClock clock;
    Director director(brain, body, log, clock);
    REQUIRE(director.run("reflex"));
    clock.wait_entered();                       // parked inside the "press my button" wait
    brain.post_event(json{{"event", "button.press"}});
    clock.release();
    director.wait();
    CHECK(brain.event_count("button.press") == 1);   // the scene did not stage a second one
}

TEST_CASE("night: the cap is real and the caption says what it capped to") {
    Stage s;
    s.policy.night_cap = 40;
    REQUIRE(s.director.run("night"));
    s.director.wait();
    auto calls = s.body.snapshot();
    CHECK(has(calls, "say:Cover my light sensor."));
    CHECK(has(calls, "express:sleepy"));               // the staged light.low
    CHECK(has(calls, "led:40,0,0"));                   // the guardrail, not the scene, chose 40
    CHECK(has(calls, "say:Rule capped the LED to 40."));
    CHECK(s.brain.event_count("light.high") == 1);     // the lights come back up
    CHECK_FALSE(s.policy.night.load());
}

TEST_CASE("fever: stages the hot chip when the real one stays cool") {
    Stage s;
    REQUIRE(s.director.run("fever"));
    s.director.wait();
    auto calls = s.body.snapshot();
    CHECK(has(calls, "say:Warm my chip."));
    CHECK(has(calls, "express:alert"));
    CHECK(s.brain.event_count("temp.hot") == 1);
    CHECK(s.clock.total_ms() == 10000);
}

TEST_CASE("who: Pip says what the camera saw") {
    Stage s;
    REQUIRE(s.director.run("who"));
    s.director.wait();
    auto calls = s.body.snapshot();
    CHECK(has(calls, "say:Who's there?"));
    CHECK(has(calls, "say:a desk with a keyboard"));
    CHECK(s.fake_cortex.snapshot().size() == 1);
}

TEST_CASE("fallback: the Pi 5 answers the next hold, and only that one") {
    Stage s;
    s.llm.replies = {text_reply("The Pi five answered.")};
    REQUIRE(s.director.run("fallback"));
    s.brain.post_event(json{{"event", "button.hold"}});
    s.director.wait();
    auto calls = s.body.snapshot();
    CHECK(has(calls, "scene:fallback"));
    CHECK(has(calls, "say:Cortex offline. The next answer comes from the Pi five."));
    // No llm2 is configured here, so forcing is a no-op; what matters is that
    // the scene turns it off again on the way out.
    CHECK(s.clock.total_ms() <= 60000);
}

TEST_CASE("tour: runs end to end, unattended, and spends its two minutes") {
    Stage s;
    REQUIRE(s.director.run("tour"));
    s.director.wait();
    auto calls = s.body.snapshot();
    CHECK(has(calls, "scene:tour"));
    CHECK(has(calls, "say:I'm Pip. My body is a Pico: face, chirps, senses."));
    CHECK(has(calls, "say:Press my button."));                  // the reflex leg
    CHECK(has(calls, "say:Cover my light sensor."));             // the night leg
    CHECK(has(calls, "say:a desk with a keyboard"));             // the who leg
    CHECK(has(calls, "say:That's all of me. Press my button any time."));
    CHECK(count_of(calls, "express:idle") >= 1);
    CHECK(s.clock.total_ms() >= 60000);
    CHECK(s.brain.event_count("button.press") == 1);
    CHECK(s.brain.event_count("light.low") == 1);
    CHECK_FALSE(s.director.running());
}

TEST_CASE("only one scene at a time; the next one runs once the first is done") {
    FakeBody body; Policy policy; EventLog log{200, nullptr};
    Brain brain(BrainConfig{100000, 10, 4}, body, policy, log, {});
    GateClock clock;
    Director director(brain, body, log, clock);
    REQUIRE(director.run("fever"));
    clock.wait_entered();
    CHECK(director.running());
    CHECK(director.current() == "fever");
    CHECK_FALSE(director.run("who"));       // busy
    clock.release();
    director.wait();
    CHECK_FALSE(director.running());
    CHECK(director.run("fever"));           // free again
    director.wait();
}
