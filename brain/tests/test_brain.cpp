#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "brain.hpp"
#include "fake_body.hpp"
#include "fake_llm.hpp"
#include <algorithm>
#include <chrono>
#include <thread>
using namespace pip::brain;
using V = std::vector<std::string>;

TEST_CASE("events flow through the worker to the body; junk is rejected") {
    FakeBody body; Policy policy; EventLog log{100, nullptr};
    Brain brain({.senses_poll_ms = 100000}, body, policy, log, {});
    CHECK(brain.post_event(json{{"event","button.press"}}));
    CHECK_FALSE(brain.post_event(json{{"event","explode"}}));
    CHECK_FALSE(brain.post_event(json::parse("[1,2]")));
    brain.wait_idle();
    CHECK(body.snapshot() == V{"express:wink", "chirp:rise"});
    auto h = brain.health();
    CHECK(h["events"] == 1); CHECK(h["reflexes"] == 1); CHECK(h["llm_calls"] == 0); CHECK(h["llm"] == false);
}
TEST_CASE("senses poller raises temp.hot once") {
    FakeBody body; body.next_senses = {10, 40, false, true}; Policy policy; policy.hot_c = 35; EventLog log{100, nullptr};
    Brain brain({.senses_poll_ms = 50}, body, policy, log, {});
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    brain.wait_idle();
    auto calls = body.snapshot();
    CHECK(std::count(calls.begin(), calls.end(), "express:alert") == 1);
    CHECK(std::count(calls.begin(), calls.end(), "led:255,0,0") == 1);
    CHECK(std::count(calls.begin(), calls.end(), "senses") >= 3);
}
TEST_CASE("hold: thinking reflex first, then the agent reacts with context") {
    FakeLlm llm; llm.replies = {tool_call_reply("express", json{{"emotion","happy"}}), text_reply("Hello!")};
    FakeBody body; body.next_senses = {15, 26, true, true}; Policy policy; EventLog log{100, nullptr};
    JudgmentConfig jcfg; jcfg.llm_url = llm.url(); jcfg.timeout_s = 5;  // explicit: gcc -Wextra flags partial designated init
    Brain brain({.senses_poll_ms = 100000}, body, policy, log, jcfg);
    brain.post_event(json{{"event","button.press"}});
    brain.post_event(json{{"event","button.hold"}});
    brain.wait_idle();
    auto calls = body.snapshot();
    REQUIRE(calls.size() >= 4);
    CHECK(calls[0] == "express:wink"); CHECK(calls[1] == "chirp:rise"); CHECK(calls[2] == "express:thinking");
    CHECK(std::find(calls.begin(), calls.end(), "express:happy") != calls.end());
    auto reqs = llm.requests_snapshot();
    REQUIRE(reqs.size() >= 1);
    std::string user = reqs[0]["messages"][1]["content"];
    CHECK(user.find("button.press") != std::string::npos);
    CHECK(brain.health()["llm_calls"] == 2);
    auto t = brain.log_tail(50); bool note = false;
    for (auto& e : t) if (e["kind"] == "note" && e["detail"].get<std::string>().find("Hello!") != std::string::npos) note = true;
    CHECK(note);
}
TEST_CASE("a failed senses poll is never cached; hold re-probes once the body recovers") {
    FakeLlm llm; llm.replies = {text_reply("ok")};
    FakeBody body; body.fail = true; body.next_senses = {-1, 0, false, false};
    Policy policy; EventLog log{100, nullptr};
    JudgmentConfig jcfg; jcfg.llm_url = llm.url(); jcfg.timeout_s = 5;
    Brain brain({.senses_poll_ms = 50}, body, policy, log, jcfg);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));  // a few failing poll ticks
    brain.wait_idle();
    auto calls0 = body.snapshot();  // one copy: begin()/end() must come from the same container
    CHECK(std::count(calls0.begin(), calls0.end(), std::string("senses")) >= 2);

    body.fail = false; body.next_senses = {15, 26, true, true};  // "the Pico may boot later"
    brain.post_event(json{{"event","button.hold"}});
    brain.wait_idle();

    auto reqs = llm.requests_snapshot();
    REQUIRE(reqs.size() >= 1);
    std::string user = reqs[0]["messages"][1]["content"];
    CHECK(user.find("light_lux=15") != std::string::npos);
    CHECK(user.find("light_lux=-1") == std::string::npos);
}
