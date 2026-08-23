#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "judgment.hpp"
#include "fake_body.hpp"
#include "fake_llm.hpp"
#include "fake_services.hpp"
using namespace pip::brain;
using V = std::vector<std::string>;

namespace {
// Everything a Judgment needs except the model, which each test wires itself.
struct Rig {
    FakeBody body;
    Policy policy;
    EventLog log{50, nullptr};
    Reflex rx{body, policy, log};
};
// The machinery ticker rides the HUD; body-action tests look past it.
static V acts(const V& c) {
    V out;
    for (auto& s : c) if (s.rfind("hud:", 0) != 0) out.push_back(s);
    return out;
}
static bool hud_has(const V& c, const std::string& needle) {
    for (auto& s : c) if (s.rfind("hud:", 0) == 0 && s.find(needle) != std::string::npos) return true;
    return false;
}
}  // namespace

TEST_CASE("hold: agent calls express through the body tool, usage and latency are logged") {
    FakeLlm llm; llm.replies = {tool_call_reply("express", json{{"emotion","happy"}}), text_reply("I smiled at you.")};
    Rig r;
    JudgmentConfig jcfg; jcfg.llm_url = llm.url(); jcfg.model = "fake"; jcfg.timeout_s = 5;  // explicit: gcc -Wextra flags partial designated init
    Judgment j(jcfg, r.body, r.rx, r.log);
    REQUIRE(j.enabled());
    Context ctx; ctx.recent_events = {"3s ago button.press"}; ctx.senses = {12.0, 27.0, true, true};
    Verdict v = j.react("button.hold", ctx);
    CHECK(v.reply == "I smiled at you.");
    CHECK(v.mind == 'J');
    CHECK(v.ms >= 0);
    // No speaker: the closing sentence still reaches the bubble.
    CHECK(acts(r.body.snapshot()) == V{"express:happy", "say:I smiled at you."});
    // The machinery ticker narrated the run on the HUD caption.
    CHECK(hud_has(r.body.snapshot(), "deep-agent"));
    CHECK(hud_has(r.body.snapshot(), "tool express"));
    CHECK(r.log.count(EventLog::Kind::Llm) == 2);        // one entry per model round trip
    auto t = r.log.tail(10); bool tokens = false;
    for (auto& e : t) if (e["kind"] == "llm") { CHECK(e["prompt_tokens"].get<int>() > 0); tokens = true; }
    CHECK(tokens);
    auto reqs = llm.requests_snapshot();
    REQUIRE(reqs.size() == 2);
    CHECK(reqs[0]["tools"].size() == 7);                 // express, chirp, led, senses, say, look, search
    std::string sys = reqs[0]["messages"][0]["content"];
    CHECK(sys.find("Pip") != std::string::npos);
    std::string user = reqs[0]["messages"][1]["content"];
    CHECK(user.find("button.press") != std::string::npos);
    CHECK(user.find("27") != std::string::npos);
}

TEST_CASE("the transcript the cortex heard goes into the prompt; silence is said so") {
    Context ctx;
    ctx.transcript = "what is a reflex?";
    CHECK(Judgment::user_prompt(ctx).find("They just said: \"what is a reflex?\"") != std::string::npos);
    ctx.lang = "fr";
    CHECK(Judgment::user_prompt(ctx).find("(language: fr)") != std::string::npos);
    Context quiet;
    CHECK(Judgment::user_prompt(quiet).find("held your button and said nothing") != std::string::npos);
}

TEST_CASE("earlier exchanges land in the prompt, oldest first") {
    Context ctx;
    ctx.transcript = "and the second one?";
    ctx.dialogue = {{"name a planet", "Mars, the rusty one."}};
    auto p = Judgment::user_prompt(ctx);
    auto a = p.find("They said: \"name a planet\"");
    auto b = p.find("You replied: \"Mars, the rusty one.\"");
    auto c = p.find("They just said: \"and the second one?\"");
    REQUIRE(a != std::string::npos);
    REQUIRE(b != std::string::npos);
    REQUIRE(c != std::string::npos);
    CHECK(a < b);
    CHECK(b < c);
}

TEST_CASE("strip_think removes thought blocks, closed or not") {
    CHECK(Judgment::strip_think("<think>hmm</think>The answer.") == "The answer.");
    CHECK(Judgment::strip_think("A<think>x</think>B<think>y</think>C") == "ABC");
    CHECK(Judgment::strip_think("<think>ran out of tok") == "");
    CHECK(Judgment::strip_think("no thoughts here") == "no thoughts here");
}

TEST_CASE("the say tool speaks through the speaker and is not repeated at the end") {
    FakeLlm llm; llm.replies = {tool_call_reply("say", json{{"text","A reflex is a rule, not a thought."}}),
                                text_reply("A reflex is a rule, not a thought.")};
    Rig r; FakeVoice voice; Voice v(voice.url());
    Speaker sp(r.body, v, r.log);
    JudgmentConfig jcfg; jcfg.llm_url = llm.url(); jcfg.timeout_s = 5;
    Judgment j(jcfg, r.body, r.rx, r.log, &sp);
    Verdict verdict = j.react("button.hold", Context{});
    sp.wait_idle();
    CHECK(verdict.reply == "A reflex is a rule, not a thought.");
    auto calls = acts(r.body.snapshot());
    REQUIRE(calls.size() == 2);
    CHECK(calls[0] == "say:A reflex is a rule, not a thought.");
    CHECK(calls[1].rfind("speak:", 0) == 0);          // said once, spoken once
}

TEST_CASE("the look tool hands the model what the cortex saw") {
    FakeLlm llm; llm.replies = {tool_call_reply("look", json{{"question","who is there?"}}),
                                text_reply("I see a desk with a keyboard.")};
    Rig r; FakeCortex cortex; Cortex c(cortex.url());
    JudgmentConfig jcfg; jcfg.llm_url = llm.url(); jcfg.timeout_s = 5;
    Judgment j(jcfg, r.body, r.rx, r.log, nullptr, &c);
    Verdict v = j.react("button.hold", Context{});
    CHECK(v.reply == "I see a desk with a keyboard.");
    CHECK(cortex.snapshot() == V{"see:who is there?"});
    auto reqs = llm.requests_snapshot();
    REQUIRE(reqs.size() == 2);
    // The tool result the second round trip carried is what the model answered from.
    std::string tool_msg = reqs[1]["messages"].back()["content"];
    CHECK(tool_msg.find("a desk with a keyboard") != std::string::npos);
}

TEST_CASE("a blind Pip says so instead of failing the tool call") {
    FakeLlm llm; llm.replies = {tool_call_reply("look", json{{"question","who is there?"}}), text_reply("I can't see right now.")};
    Rig r;
    JudgmentConfig jcfg; jcfg.llm_url = llm.url(); jcfg.timeout_s = 5;
    Judgment j(jcfg, r.body, r.rx, r.log);          // no cortex at all
    Verdict v = j.react("button.hold", Context{});
    CHECK(v.mind == 'J');
    auto reqs = llm.requests_snapshot();
    REQUIRE(reqs.size() == 2);
    std::string tool_msg = reqs[1]["messages"].back()["content"];
    CHECK(tool_msg.find("I can't see right now") != std::string::npos);
}

TEST_CASE("mind: the Pi 5 answers when the Jetson is down") {
    FakeLlm primary; primary.fail = true;
    FakeLlm second; second.replies = {text_reply("The Pi five is thinking for me.")};
    Rig r;
    JudgmentConfig jcfg; jcfg.llm_url = primary.url(); jcfg.llm2_url = second.url(); jcfg.timeout_s = 5;
    Judgment j(jcfg, r.body, r.rx, r.log);
    Verdict v = j.react("button.hold", Context{});
    CHECK(v.reply == "The Pi five is thinking for me.");
    CHECK(v.mind == '5');
}

TEST_CASE("mind: nobody answers when both are down") {
    Rig r;
    JudgmentConfig jcfg; jcfg.llm_url = "http://127.0.0.1:9"; jcfg.llm2_url = "http://127.0.0.1:9"; jcfg.timeout_s = 1;
    Judgment j(jcfg, r.body, r.rx, r.log);
    Verdict v = j.react("button.hold", Context{});
    CHECK(v.reply.empty());
    CHECK(v.mind == '-');
    CHECK(r.log.count(EventLog::Kind::Note) >= 1);
}

TEST_CASE("the fallback scene sends the question straight to the Pi 5") {
    FakeLlm primary; FakeLlm second; second.replies = {text_reply("Slow but here.")};
    Rig r;
    JudgmentConfig jcfg; jcfg.llm_url = primary.url(); jcfg.llm2_url = second.url(); jcfg.timeout_s = 5;
    Judgment j(jcfg, r.body, r.rx, r.log);
    CHECK_FALSE(j.forced());
    j.force_fallback(true);
    CHECK(j.forced());
    Verdict v = j.react("button.hold", Context{});
    CHECK(v.reply == "Slow but here.");
    CHECK(v.mind == '5');
    CHECK(primary.requests_snapshot().empty());   // the Jetson was never asked
    j.force_fallback(false);
    CHECK_FALSE(j.forced());
}

TEST_CASE("forcing the fallback with nothing to fall back to is ignored") {
    FakeLlm llm; llm.replies = {text_reply("Still me.")};
    Rig r;
    JudgmentConfig jcfg; jcfg.llm_url = llm.url(); jcfg.timeout_s = 5;
    Judgment j(jcfg, r.body, r.rx, r.log);
    j.force_fallback(true);
    CHECK_FALSE(j.forced());
    Verdict v = j.react("button.hold", Context{});
    CHECK(v.mind == 'J');
}

TEST_CASE("night guardrail applies to the model's led call") {
    FakeLlm llm; llm.replies = {tool_call_reply("led", json{{"r",255},{"g",255},{"b",255}}), text_reply("Bright!")};
    Rig r; r.policy.night = true; r.policy.night_cap = 40;
    JudgmentConfig jcfg; jcfg.llm_url = llm.url(); jcfg.timeout_s = 5;  // explicit: gcc -Wextra flags partial designated init
    Judgment j(jcfg, r.body, r.rx, r.log);
    j.react("button.hold", Context{});
    CHECK(acts(r.body.snapshot()) == V{"led:40,40,40", "say:Bright!"});
    // The guardrail announced itself on the ticker too.
    CHECK(hud_has(r.body.snapshot(), "rete night-cap"));
}

TEST_CASE("disabled without a URL; unreachable server returns empty and logs") {
    Rig r;
    Judgment off({}, r.body, r.rx, r.log);
    CHECK_FALSE(off.enabled());
    CHECK(off.react("button.hold", Context{}).reply == "");
    JudgmentConfig dead_cfg; dead_cfg.llm_url = "http://127.0.0.1:9"; dead_cfg.timeout_s = 1;  // explicit: gcc -Wextra flags partial designated init
    Judgment dead(dead_cfg, r.body, r.rx, r.log);
    Verdict v = dead.react("button.hold", Context{});
    CHECK(v.reply == "");
    CHECK(v.mind == '-');
    CHECK(r.log.count(EventLog::Kind::Note) >= 1);
}

TEST_CASE("a config that makes DeepAgent's constructor throw is caught, not propagated") {
    FakeLlm llm;
    Rig r;
    JudgmentConfig jcfg; jcfg.llm_url = llm.url(); jcfg.max_iterations = 0;  // explicit: gcc -Wextra flags partial designated init
    Judgment j(jcfg, r.body, r.rx, r.log);
    Verdict v;
    CHECK_NOTHROW(v = j.react("button.hold", Context{}));
    CHECK(v.reply == "");
    CHECK(v.mind == '-');
    CHECK(r.log.count(EventLog::Kind::Note) >= 1);
}
