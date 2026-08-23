#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "brain.hpp"
#include "fake_body.hpp"
#include "fake_llm.hpp"
#include "fake_services.hpp"
#include <algorithm>
#include <chrono>
#include <thread>
using namespace pip::brain;
using V = std::vector<std::string>;

namespace {
constexpr BrainConfig quiet{100000, 10, 4};   // no senses polling during the test
constexpr BrainConfig ticking{50, 10, 4};

bool has(const V& calls, const std::string& s) { return std::find(calls.begin(), calls.end(), s) != calls.end(); }
bool has_prefix(const V& calls, const std::string& p) {
    return std::any_of(calls.begin(), calls.end(), [&](const std::string& s) { return s.rfind(p, 0) == 0; });
}
// The HUD push carrying a given key, or "" if there was none.
std::string hud_with(const V& calls, const std::string& key) {
    for (auto& c : calls)
        if (c.rfind("hud:", 0) == 0 && c.find("\"" + key + "\"") != std::string::npos) return c;
    return "";
}
}  // namespace

TEST_CASE("events flow through the worker to the body; junk is rejected") {
    FakeBody body; Policy policy; EventLog log{100, nullptr};
    Brain brain(quiet, body, policy, log, {});
    CHECK(brain.post_event(json{{"event","button.press"}}));
    CHECK_FALSE(brain.post_event(json{{"event","explode"}}));
    CHECK_FALSE(brain.post_event(json::parse("[1,2]")));
    brain.wait_idle();
    auto calls = body.snapshot();
    CHECK(calls[0] == "express:wink");
    CHECK(calls[1] == "chirp:rise");
    CHECK(has_prefix(calls, "hud:"));
    auto h = brain.health();
    CHECK(h["events"] == 1); CHECK(h["reflexes"] == 1); CHECK(h["llm_calls"] == 0); CHECK(h["llm"] == false);
}

TEST_CASE("every event pushes its microseconds to the HUD") {
    FakeBody body; Policy policy; EventLog log{100, nullptr};
    Brain brain(quiet, body, policy, log, {});
    brain.post_event(json{{"event","button.press"}});
    brain.wait_idle();
    std::string push = hud_with(body.snapshot(), "reflex_us");
    REQUIRE_FALSE(push.empty());
    auto j = json::parse(push.substr(4));
    CHECK(j["reflex_us"].get<long>() >= 0);
    CHECK(j.size() == 1);          // nothing else on the strip was overwritten
}

TEST_CASE("senses poller raises temp.hot once") {
    FakeBody body; body.next_senses = {10, 40, false, true}; Policy policy; policy.hot_c = 35; EventLog log{100, nullptr};
    Brain brain(ticking, body, policy, log, {});
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    brain.wait_idle();
    auto calls = body.snapshot();
    CHECK(std::count(calls.begin(), calls.end(), "express:alert") == 1);
    CHECK(std::count(calls.begin(), calls.end(), "led:255,0,0") == 1);
    CHECK(std::count(calls.begin(), calls.end(), "senses") >= 3);
}

TEST_CASE("hold: listen, think, answer, and the HUD carries the numbers") {
    FakeLlm llm; llm.replies = {tool_call_reply("express", json{{"emotion","happy"}}), text_reply("Hello!")};
    FakeCortex cortex; FakeVoice voice;
    Cortex c(cortex.url()); Voice v(voice.url());
    FakeBody body; body.next_senses = {15, 26, true, true}; Policy policy; EventLog log{100, nullptr};
    JudgmentConfig jcfg; jcfg.llm_url = llm.url(); jcfg.timeout_s = 5;  // explicit: gcc -Wextra flags partial designated init
    Brain brain(quiet, body, policy, log, jcfg, &c, &v);
    brain.post_event(json{{"event","button.press"}});
    brain.post_event(json{{"event","button.hold"}});
    brain.wait_idle();
    auto calls = body.snapshot();
    CHECK(calls[0] == "express:wink");
    CHECK(has(calls, "express:listening"));
    CHECK(has(calls, "express:thinking"));
    CHECK(has(calls, "express:happy"));
    CHECK(has(calls, "say:Hello!"));
    CHECK_FALSE(has(calls, "chirp:drop"));
    std::string push = hud_with(calls, "judge_ms");
    REQUIRE_FALSE(push.empty());
    auto j = json::parse(push.substr(4));
    CHECK(j["mind"] == "J");
    CHECK(j["cortex"] == true);
    CHECK(j["judge_ms"].get<long>() >= 0);
    // The cortex heard something and the model was told what it was.
    auto reqs = llm.requests_snapshot();
    REQUIRE(reqs.size() >= 1);
    std::string user = reqs[0]["messages"][1]["content"];
    CHECK(user.find("what do you see") != std::string::npos);
    CHECK(user.find("button.press") != std::string::npos);
    auto h = brain.health();
    CHECK(h["mind"] == "J");
    CHECK(h["judge_ms"].get<long>() >= 0);
    CHECK(h["link"] == true);
    CHECK(h.contains("cortex")); CHECK(h.contains("voice")); CHECK(h.contains("scene"));
}

TEST_CASE("with the cortex down Pip still answers, and the HUD says the cortex is out") {
    FakeLlm llm; llm.replies = {text_reply("I heard nothing but I'm here.")};
    FakeCortex cortex; cortex.fail = true;
    Cortex c(cortex.url());
    FakeBody body; Policy policy; EventLog log{100, nullptr};
    JudgmentConfig jcfg; jcfg.llm_url = llm.url(); jcfg.timeout_s = 5;
    Brain brain(quiet, body, policy, log, jcfg, &c);
    brain.post_event(json{{"event","button.hold"}});
    brain.wait_idle();
    auto calls = body.snapshot();
    CHECK(has(calls, "say:I heard nothing but I'm here."));
    auto j = json::parse(hud_with(calls, "judge_ms").substr(4));
    CHECK(j["cortex"] == false);
    auto reqs = llm.requests_snapshot();
    REQUIRE(reqs.size() >= 1);
    std::string user = reqs[0]["messages"][1]["content"];
    CHECK(user.find("said nothing") != std::string::npos);   // the no-transcript prompt
}

TEST_CASE("no answer at all earns a drop chirp") {
    FakeBody body; Policy policy; EventLog log{100, nullptr};
    JudgmentConfig jcfg; jcfg.llm_url = "http://127.0.0.1:9"; jcfg.timeout_s = 1;
    Brain brain(quiet, body, policy, log, jcfg);
    brain.post_event(json{{"event","button.hold"}});
    brain.wait_idle();
    auto calls = body.snapshot();
    CHECK(has(calls, "chirp:drop"));
    auto j = json::parse(hud_with(calls, "judge_ms").substr(4));
    CHECK(j["mind"] == "-");
}

TEST_CASE("an injected event runs the reflex and is logged as staged") {
    FakeBody body; Policy policy; EventLog log{100, nullptr};
    Brain brain(quiet, body, policy, log, {});
    brain.inject("button.press");
    brain.inject("not.an.event");
    brain.wait_idle();
    CHECK(has(body.snapshot(), "express:wink"));
    bool staged = false, refused = false;
    for (auto& e : log.tail(50)) {
        if (e["kind"] != "note") continue;
        auto d = e["detail"].get<std::string>();
        if (d.find("simulated event button.press") != std::string::npos) staged = true;
        if (d.find("inject: unknown event") != std::string::npos) refused = true;
    }
    CHECK(staged);
    CHECK(refused);
}

TEST_CASE("the scene caption goes to the HUD and to health") {
    FakeBody body; Policy policy; EventLog log{100, nullptr};
    Brain brain(quiet, body, policy, log, {});
    brain.set_scene_name("tour");
    CHECK(brain.scene_name() == "tour");
    CHECK(brain.health()["scene"] == "tour");
    CHECK(has(body.snapshot(), R"(hud:{"scene":"tour"})"));
    brain.set_scene_name("");
    CHECK(brain.health()["scene"] == "");
}

TEST_CASE("look asks the cortex; a brain with no eyes says so") {
    FakeCortex cortex; Cortex c(cortex.url());
    FakeBody body; Policy policy; EventLog log{100, nullptr};
    Brain seeing(quiet, body, policy, log, {}, &c);
    CHECK(seeing.look("who is there?") == "a desk with a keyboard");
    Brain blind(quiet, body, policy, log, {});
    CHECK(blind.look("who is there?") == "I can't see right now");
}

TEST_CASE("led_capped goes through the night cap and reports what landed") {
    FakeBody body; Policy policy; policy.night_cap = 40; EventLog log{100, nullptr};
    Brain brain(quiet, body, policy, log, {});
    CHECK(brain.led_capped(255, 0, 0) == 255);
    policy.night = true;
    CHECK(brain.led_capped(255, 0, 0) == 40);
    CHECK(has(body.snapshot(), "led:40,0,0"));
}

TEST_CASE("a failed senses poll is never cached; hold re-probes once the body recovers") {
    FakeLlm llm; llm.replies = {text_reply("ok")};
    FakeBody body; body.fail = true; body.next_senses = {-1, 0, false, false};
    Policy policy; EventLog log{100, nullptr};
    JudgmentConfig jcfg; jcfg.llm_url = llm.url(); jcfg.timeout_s = 5;
    Brain brain(ticking, body, policy, log, jcfg);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));  // a few failing poll ticks
    brain.wait_idle();
    auto calls0 = body.snapshot();  // one copy: begin()/end() must come from the same container
    CHECK(std::count(calls0.begin(), calls0.end(), std::string("senses")) >= 2);

    body.set_senses({15, 26, true, true}, false);  // "the Pico may boot later"
    brain.post_event(json{{"event","button.hold"}});
    brain.wait_idle();

    auto reqs = llm.requests_snapshot();
    REQUIRE(reqs.size() >= 1);
    std::string user = reqs[0]["messages"][1]["content"];
    CHECK(user.find("light_lux=15") != std::string::npos);
    CHECK(user.find("light_lux=-1") == std::string::npos);
}
