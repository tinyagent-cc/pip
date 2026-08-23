#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "reflex.hpp"
#include "fake_body.hpp"
using namespace pip::brain;
using V = std::vector<std::string>;

struct Rig { FakeBody body; Policy policy; EventLog log{100, nullptr}; Reflex rx{body, policy, log}; };

TEST_CASE("press winks and chirps, release is matched silently") {
    Rig r;
    CHECK(r.rx.on_event("button.press", 1000) == 1);
    CHECK(r.body.snapshot() == V{"express:wink", "chirp:rise"});
    CHECK(r.rx.on_event("button.release", 1100) == 1);
    CHECK(r.body.snapshot() == V{"express:wink", "chirp:rise"});
    CHECK(r.log.count(EventLog::Kind::Reflex) == 2);
}
TEST_CASE("a second press inside the chirp gap winks without chirping") {
    Rig r; r.policy.chirp_gap_ms = 5000;
    r.rx.on_event("button.press", 1000);
    r.rx.on_event("button.press", 2000);
    CHECK(r.body.snapshot() == V{"express:wink", "chirp:rise", "express:wink"});
    r.rx.on_event("button.press", 7001);
    CHECK(r.body.snapshot().back() == "chirp:rise");
}
TEST_CASE("dark then bright: sleepy, then alert + trill; bright when already bright does nothing") {
    Rig r;
    CHECK(r.rx.on_event("light.high", 10) == 1);                 // bright-note only
    CHECK(r.body.snapshot().empty());
    CHECK(r.rx.on_event("light.low", 100) == 1);
    CHECK(r.body.snapshot() == V{"express:sleepy"});
    CHECK(r.policy.night.load()); CHECK(r.rx.room_dark());
    CHECK(r.rx.on_event("light.high", 200) == 2);                // bright-alert + bright-note
    CHECK(r.body.snapshot() == V{"express:sleepy", "express:alert", "chirp:trill"});
    CHECK_FALSE(r.policy.night.load()); CHECK_FALSE(r.rx.room_dark());
    CHECK(r.rx.on_event("light.high", 300) == 1);
    CHECK(r.body.snapshot().size() == 3);
}
TEST_CASE("hold shows thinking; unknown events match nothing") {
    Rig r;
    CHECK(r.rx.on_event("button.hold", 1) == 1);
    CHECK(r.body.snapshot() == V{"express:thinking"});
    CHECK(r.rx.on_event("bogus.event", 2) == 0);
}
TEST_CASE("temp.hot fires on the transition only, LED red capped at night") {
    Rig r; r.policy.hot_c = 35; r.policy.night_cap = 40;
    Senses cool{10, 30, false, true}, hot{10, 36, false, true};
    CHECK_FALSE(r.rx.on_senses(cool, 1));
    CHECK(r.rx.on_senses(hot, 2));
    CHECK(r.body.snapshot() == V{"express:alert", "led:255,0,0"});
    CHECK_FALSE(r.rx.on_senses(hot, 3));                         // still hot, no refire
    CHECK_FALSE(r.rx.on_senses(cool, 4));
    r.rx.on_event("light.low", 5);
    CHECK(r.rx.on_senses(hot, 6));
    CHECK(r.body.snapshot().back() == "led:40,0,0");
}
TEST_CASE("reflex timing is logged in microseconds") {
    Rig r; r.rx.on_event("button.press", 1);
    auto t = r.log.tail(5);
    bool seen = false;
    for (auto& e : t) if (e["kind"] == "reflex" && e["name"] == "press-wink") { seen = true; CHECK(e["micros"].get<int64_t>() >= 0); CHECK(e["micros"].get<int64_t>() < 50000); }
    CHECK(seen);
}
