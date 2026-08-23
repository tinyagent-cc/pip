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
TEST_CASE("hold opens the ears; unknown events match nothing") {
    Rig r;
    CHECK(r.rx.on_event("button.hold", 1) == 1);
    CHECK(r.body.snapshot() == V{"express:listening", "chirp:rise"});
    CHECK(r.rx.on_event("bogus.event", 2) == 0);
}

TEST_CASE("a hold right after a press listens without a second chirp") {
    Rig r; r.policy.chirp_gap_ms = 5000;
    r.rx.on_event("button.press", 1000);
    r.rx.on_event("button.hold", 1500);
    CHECK(r.body.snapshot() == V{"express:wink", "chirp:rise", "express:listening"});
}

TEST_CASE("on_event records its own duration for the HUD") {
    Rig r;
    r.rx.on_event("button.press", 1);
    CHECK(r.rx.last_event_us() >= 0);
    CHECK(r.rx.last_event_us() < 50000);
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
TEST_CASE("a reflex action that throws does not corrupt later events") {
    Rig r;
    r.body.throw_on_express = true;
    CHECK_NOTHROW(r.rx.on_event("button.press", 1000));
    CHECK(r.body.snapshot().empty());
    r.body.throw_on_express = false;
    CHECK(r.rx.on_event("button.press", 1100) == 1);
    CHECK(r.body.snapshot() == V{"express:wink", "chirp:rise"});
    bool seen_note = false;
    for (auto& e : r.log.tail(10))
        if (e["kind"] == "note" && e["detail"].get<std::string>().find("reflex action threw") != std::string::npos) seen_note = true;
    CHECK(seen_note);
}
