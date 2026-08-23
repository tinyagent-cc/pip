#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "body.hpp"
#include "fake_body.hpp"
using namespace pip::brain;

TEST_CASE("HttpBody speaks protocol v0 to a fake Pip") {
    FakePip pip; pip.senses = {12.5, 28.0, false, true};
    HttpBody body(pip.url());
    CHECK(body.express("wink"));
    CHECK(body.chirp("rise"));
    CHECK(body.led(1, 2, 3));
    Senses s = body.senses();
    CHECK(s.ok); CHECK(s.light_lux == doctest::Approx(12.5)); CHECK(s.temp_c == doctest::Approx(28.0)); CHECK_FALSE(s.button_down);
    CHECK(pip.snapshot() == std::vector<std::string>{"express:wink", "chirp:rise", "led:1,2,3", "senses"});
}
TEST_CASE("HttpBody reports 400 and unreachable as false") {
    FakePip pip;
    HttpBody body(pip.url());
    CHECK_FALSE(body.express("angry"));   // not a v0 emotion
    HttpBody dead("http://127.0.0.1:9", 200);
    CHECK_FALSE(dead.express("wink"));
    CHECK_FALSE(dead.senses().ok);
}
TEST_CASE("HttpBody::senses() is total: a wrong-typed field on a 200 reply yields ok=false, no throw") {
    FakePip pip;
    pip.senses_override_body = R"({"light_lux":"bright","temp_c":28.0,"button":"up"})";
    HttpBody body(pip.url());
    Senses s;
    CHECK_NOTHROW(s = body.senses());
    CHECK_FALSE(s.ok);
}

TEST_CASE("HudFields serialises only the fields that were set") {
    HudFields f;
    CHECK(f.to_json() == json::object());
    f.reflex_us = 95;
    f.mind = 'J';
    json j = f.to_json();
    CHECK(j.size() == 2);
    CHECK(j["reflex_us"] == 95);
    CHECK(j["mind"] == "J");
    CHECK_FALSE(j.contains("judge_ms"));
    f.judge_ms = 5800; f.brain = true; f.cortex = false; f.scene = "tour";
    j = f.to_json();
    CHECK(j["judge_ms"] == 5800);
    CHECK(j["brain"] == true);
    CHECK(j["cortex"] == false);
    CHECK(j["scene"] == "tour");
}

TEST_CASE("HttpBody v1: say, hud, scene and ping reach the body; speak does not") {
    FakePip pip;
    HttpBody body(pip.url());
    CHECK(body.say("hello there"));
    HudFields f; f.judge_ms = 5800; f.mind = '5';
    CHECK(body.hud(f));
    CHECK(body.scene("tour"));
    CHECK(body.ping());
    CHECK(body.alive());
    CHECK_FALSE(body.speak(std::vector<int16_t>(32, 0)));   // no audio path over HTTP
    CHECK(pip.snapshot() == std::vector<std::string>{
        "say:hello there", R"(hud:{"judge_ms":5800,"mind":"5"})", "scene:tour", "ping"});
}

TEST_CASE("HttpBody::say truncates to the body's 95-character bubble") {
    FakePip pip;
    HttpBody body(pip.url());
    CHECK(body.say(std::string(200, 'x')));
    CHECK(pip.snapshot().at(0) == "say:" + std::string(95, 'x'));
}

TEST_CASE("HttpBody::alive() follows the last call, so a dead body reads false") {
    HttpBody dead("http://127.0.0.1:9", 200);
    CHECK_FALSE(dead.alive());          // nothing succeeded yet
    CHECK_FALSE(dead.say("anyone?"));
    CHECK_FALSE(dead.alive());
}
