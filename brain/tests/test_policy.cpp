#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "policy.hpp"
#include "log.hpp"
using namespace pip::brain;

TEST_CASE("clamp caps at night only") {
    Policy p; p.night_cap = 40;
    CHECK(p.clamp(255) == 255);
    CHECK(p.clamp(-5) == 0);
    p.night = true;
    CHECK(p.clamp(255) == 40);
    CHECK(p.clamp(10) == 10);
}
TEST_CASE("chirp gap") {
    Policy p; p.chirp_gap_ms = 5000;
    CHECK(p.chirp_allowed(1000));
    CHECK_FALSE(p.chirp_allowed(3000));
    CHECK(p.chirp_allowed(6000));
}
TEST_CASE("log counts survive the ring wrapping") {
    EventLog log(3, nullptr);
    for (int i = 0; i < 5; ++i) log.reflex("r", 10, "x");
    log.llm("hold", 1200, 30, 5, "ok");
    CHECK(log.count(EventLog::Kind::Reflex) == 5);
    CHECK(log.count(EventLog::Kind::Llm) == 1);
    CHECK(log.tail(10).size() == 3);
    CHECK(log.tail(1)[0]["kind"] == "llm");
    CHECK(log.tail(1)[0]["micros"] == 1200000);
}
