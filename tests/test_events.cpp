#include <cstring>
#include "check.h"
#include "pip/events.hpp"
using namespace pip;
static void run() {
    ButtonFsm b(1500, 30);
    CHECK(b.tick(0, false) == Event::None);
    CHECK(b.tick(10, true) == Event::None);         // bounce window
    CHECK(b.tick(20, false) == Event::None);        // bounced back
    CHECK(b.tick(30, true) == Event::None);
    CHECK(b.tick(70, true) == Event::ButtonPress);  // stable 40ms
    CHECK(b.down());
    CHECK(b.tick(1000, true) == Event::None);
    CHECK(b.tick(1600, true) == Event::ButtonHold); // 1530ms after the accepted press at 70
    CHECK(b.tick(1700, true) == Event::None);       // hold fires once
    CHECK(b.tick(1710, false) == Event::None);
    CHECK(b.tick(1750, false) == Event::ButtonRelease);
    CHECK(!b.down());
    // short press never holds
    CHECK(b.tick(2000, true) == Event::None); CHECK(b.tick(2040, true) == Event::ButtonPress);
    CHECK(b.tick(2100, false) == Event::None); CHECK(b.tick(2140, false) == Event::ButtonRelease);
    CHECK_STREQ(event_name(Event::ButtonHold), "button.hold");
    CHECK_STREQ(event_name(Event::LightLow), "light.low");

    LightFsm l(10.0f, 20.0f, 30000);
    CHECK(l.tick(0, 100.0f) == Event::None);
    CHECK(l.tick(1000, 5.0f) == Event::None);        // starts timing
    CHECK(l.tick(20000, 5.0f) == Event::None);
    CHECK(l.tick(25000, 50.0f) == Event::None);      // interrupted, timer resets
    CHECK(l.tick(26000, 5.0f) == Event::None);
    CHECK(l.tick(55000, 5.0f) == Event::None);       // 29s, not yet
    CHECK(l.tick(56000, 5.0f) == Event::LightLow);   // 30s sustained
    CHECK(l.tick(57000, 5.0f) == Event::None);       // fires once
    CHECK(l.tick(58000, 15.0f) == Event::None);      // between thresholds: hysteresis holds
    CHECK(l.tick(59000, 25.0f) == Event::LightHigh);
    CHECK(l.tick(60000, 25.0f) == Event::None);
}
TEST_MAIN()
