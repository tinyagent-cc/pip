#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "reflex.hpp"
#include "fake_body.hpp"
using namespace pip::brain;
using namespace tiny_agent;

static LLMResponse with_calls(std::vector<ToolCall> calls) {
    LLMResponse r; r.message = Message::assistant(""); r.message.tool_calls = std::move(calls); r.finish_reason = "tool_calls"; return r;
}
TEST_CASE("night caps LED args the model asked for; day leaves them alone") {
    FakeBody body; Policy policy; policy.night_cap = 40; EventLog log{50, nullptr}; Reflex rx{body, policy, log};
    auto mw = rx.guardrail_middleware();
    std::vector<Message> msgs{Message::user("hi")};
    auto next = [](std::vector<Message>&) { return with_calls({ToolCall{"c1", "led", json{{"r",255},{"g",20},{"b",100}}}}); };
    auto day = mw(msgs, next);
    CHECK(day.message.tool_calls[0].arguments["r"] == 255);
    policy.night = true;
    auto night = mw(msgs, next);
    CHECK(night.message.tool_calls[0].arguments["r"] == 40);
    CHECK(night.message.tool_calls[0].arguments["g"] == 20);
    CHECK(night.message.tool_calls[0].arguments["b"] == 40);
}
TEST_CASE("second chirp inside the gap is vetoed, express untouched") {
    FakeBody body; Policy policy; policy.chirp_gap_ms = 5000; EventLog log{50, nullptr}; Reflex rx{body, policy, log};
    auto mw = rx.guardrail_middleware();
    std::vector<Message> msgs{Message::user("hi")};
    auto next = [](std::vector<Message>&) { return with_calls({
        ToolCall{"c1", "express", json{{"emotion","happy"}}}, ToolCall{"c2", "chirp", json{{"name","rise"}}}, ToolCall{"c3", "chirp", json{{"name","trill"}}}}); };
    auto r = mw(msgs, next);
    REQUIRE(r.message.tool_calls.size() == 2);
    CHECK(r.message.tool_calls[0].name == "express");
    CHECK(r.message.tool_calls[1].arguments["name"] == "rise");
    CHECK(r.raw.contains("reflex_vetoes"));
}
