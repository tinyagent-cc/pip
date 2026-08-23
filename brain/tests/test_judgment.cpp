#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "judgment.hpp"
#include "fake_body.hpp"
#include "fake_llm.hpp"
using namespace pip::brain;
using V = std::vector<std::string>;

TEST_CASE("hold: agent calls express through the body tool, usage and latency are logged") {
    FakeLlm llm; llm.replies = {tool_call_reply("express", json{{"emotion","happy"}}), text_reply("I smiled at you.")};
    FakeBody body; Policy policy; EventLog log{50, nullptr}; Reflex rx{body, policy, log};
    Judgment j({.llm_url = llm.url(), .model = "fake", .timeout_s = 5}, body, rx, log);
    REQUIRE(j.enabled());
    Context ctx; ctx.recent_events = {"3s ago button.press"}; ctx.senses = {12.0, 27.0, true, true};
    std::string out = j.react("button.hold", ctx);
    CHECK(out == "I smiled at you.");
    CHECK(body.snapshot() == V{"express:happy"});
    CHECK(log.count(EventLog::Kind::Llm) == 2);        // one entry per model round trip
    auto t = log.tail(10); bool tokens = false;
    for (auto& e : t) if (e["kind"] == "llm") { CHECK(e["prompt_tokens"].get<int>() > 0); tokens = true; }
    CHECK(tokens);
    REQUIRE(llm.requests.size() == 2);
    CHECK(llm.requests[0]["tools"].size() == 4);
    std::string sys = llm.requests[0]["messages"][0]["content"];
    CHECK(sys.find("Pip") != std::string::npos);
    std::string user = llm.requests[0]["messages"][1]["content"];
    CHECK(user.find("button.press") != std::string::npos);
    CHECK(user.find("27") != std::string::npos);
}
TEST_CASE("night guardrail applies to the model's led call") {
    FakeLlm llm; llm.replies = {tool_call_reply("led", json{{"r",255},{"g",255},{"b",255}}), text_reply("Bright!")};
    FakeBody body; Policy policy; policy.night = true; policy.night_cap = 40; EventLog log{50, nullptr}; Reflex rx{body, policy, log};
    Judgment j({.llm_url = llm.url(), .timeout_s = 5}, body, rx, log);
    j.react("button.hold", Context{});
    CHECK(body.snapshot() == V{"led:40,40,40"});
}
TEST_CASE("disabled without a URL; unreachable server returns empty and logs") {
    FakeBody body; Policy policy; EventLog log{50, nullptr}; Reflex rx{body, policy, log};
    Judgment off({}, body, rx, log);
    CHECK_FALSE(off.enabled());
    Judgment dead({.llm_url = "http://127.0.0.1:9", .timeout_s = 1}, body, rx, log);
    CHECK(dead.react("button.hold", Context{}) == "");
    CHECK(log.count(EventLog::Kind::Note) >= 1);
}
