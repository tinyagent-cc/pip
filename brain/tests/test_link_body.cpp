#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "fake_body.hpp"
#include "fake_link.hpp"
#include "link_body.hpp"
#include <chrono>
using namespace pip::brain;

namespace {
// Every test that needs alive() true starts by letting the body say hello,
// exactly as the firmware does after boot.
void say_hello(FakeLink& link, LinkBody& body) {
    link.send_json(json{{"hello", {{"fw", "v1"}, {"protocol", 1}}}});
    REQUIRE(FakeLink::wait_for([&] { return body.alive(); }));
}
}  // namespace

TEST_CASE("commands go down the wire as JSON frames") {
    FakeLink link;
    LinkBody body(link.take_brain_fd());
    CHECK(body.express("wink"));
    CHECK(body.chirp("rise"));
    CHECK(body.led(1, 2, 3));
    CHECK(body.say(std::string(120, 'x')));
    HudFields f; f.reflex_us = 95;
    CHECK(body.hud(f));
    CHECK(body.scene("tour"));
    CHECK(body.ping());
    REQUIRE(link.wait_commands(7));
    auto c = link.commands();
    CHECK(c[0] == json{{"cmd", "express"}, {"emotion", "wink"}});
    CHECK(c[1] == json{{"cmd", "chirp"}, {"name", "rise"}});
    CHECK(c[2] == json{{"cmd", "led"}, {"r", 1}, {"g", 2}, {"b", 3}});
    CHECK(c[3]["cmd"] == "say");
    CHECK(c[3]["text"].get<std::string>().size() == SAY_MAX);   // truncated for the bubble
    CHECK(c[4] == json{{"cmd", "hud"}, {"reflex_us", 95}});
    CHECK(c[5] == json{{"cmd", "scene"}, {"name", "tour"}});
    CHECK(c[6] == json{{"cmd", "ping"}});
    CHECK(body.stats().tx_frames == 7u);
}

TEST_CASE("an up event reaches the sink; the link is dead until a frame arrives") {
    FakeLink link;
    LinkBody body(link.take_brain_fd());
    std::mutex m;
    std::vector<std::string> seen;
    body.set_event_sink([&](const json& j) { std::lock_guard<std::mutex> g(m); seen.push_back(j.value("event", "")); });
    CHECK_FALSE(body.alive());
    link.send_json(json{{"event", "button.press"}});
    REQUIRE(FakeLink::wait_for([&] { std::lock_guard<std::mutex> g(m); return !seen.empty(); }));
    CHECK(seen[0] == "button.press");
    CHECK(body.alive());
    CHECK(body.stats().rx_frames == 1u);
}

TEST_CASE("a brain that hears nothing pings the wire once a second until it does") {
    FakeLink link;
    LinkBody body(link.take_brain_fd());
    CHECK_FALSE(body.alive());
    REQUIRE(link.wait_commands(1, 2500));
    CHECK(link.commands()[0] == json{{"cmd", "ping"}});
    link.send_json(json{{"senses", {{"light_lux", 1.0}}}});
    REQUIRE(FakeLink::wait_for([&] { return body.alive(); }));
    size_t n = link.commands().size();
    std::this_thread::sleep_for(std::chrono::milliseconds(1300));
    CHECK(link.commands().size() == n);      // alive: no more wake pings
}

TEST_CASE("a senses frame is cached and served without touching the wire") {
    FakeLink link;
    LinkBody body(link.take_brain_fd());
    CHECK_FALSE(body.senses().ok);   // nothing cached, no fallback
    link.send_json(json{{"senses", {{"light_lux", 12.5}, {"temp_c", 28.0}, {"button", "down"}}}});
    REQUIRE(FakeLink::wait_for([&] { return body.senses().ok; }));
    Senses s = body.senses();
    CHECK(s.light_lux == doctest::Approx(12.5));
    CHECK(s.temp_c == doctest::Approx(28.0));
    CHECK(s.button_down);
    CHECK(link.commands().empty());
}

TEST_CASE("with the link dead, commands and senses fall back to HTTP") {
    FakeLink link;
    LinkBody body(link.take_brain_fd());
    FakeBody http;
    body.set_fallback(&http);
    CHECK_FALSE(body.alive());
    CHECK(body.express("wink"));
    CHECK(body.senses().ok);
    CHECK(http.snapshot() == std::vector<std::string>{"express:wink", "senses"});
    CHECK(link.commands().empty());          // nothing went down the dead wire
    CHECK_FALSE(body.speak(std::vector<int16_t>(256, 0)));   // speech has no HTTP path

    say_hello(link, body);
    CHECK(body.express("happy"));
    REQUIRE(link.wait_commands(1));
    CHECK(link.commands()[0]["emotion"] == "happy");
    CHECK(http.snapshot().size() == 2);      // the fallback stayed out of it
}

TEST_CASE("speak chunks PCM into 256-sample audio frames carrying s16le bytes") {
    FakeLink link;
    LinkBody body(link.take_brain_fd());
    say_hello(link, body);
    std::vector<int16_t> pcm(1600);
    for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = static_cast<int16_t>(i * 13 - 4000);
    CHECK(body.speak(pcm));
    REQUIRE(link.wait_audio(7));
    auto frames = link.audio();
    CHECK(frames.size() == 7);               // 6 x 256 + 64
    for (size_t i = 0; i < 6; ++i) CHECK(frames[i].size() == 512u);
    CHECK(frames[6].size() == 128u);
    // Sample 300 lives at offset 44 of frame 1, little-endian.
    uint16_t v = static_cast<uint16_t>(frames[1][88] | (frames[1][89] << 8));
    CHECK(static_cast<int16_t>(v) == pcm[300]);
    CHECK(body.stats().tx_audio == 7u);
}

TEST_CASE("speak paces long audio at real time, minus its lead") {
    FakeLink link;
    LinkBody body(link.take_brain_fd());
    say_hello(link, body);
    auto t0 = std::chrono::steady_clock::now();
    CHECK(body.speak(std::vector<int16_t>(16000, 0)));   // one second of audio
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(ms >= 700);            // 1000 ms of audio, written up to 200 ms ahead
    CHECK(ms < 1200);
    CHECK(body.stats().tx_audio == 63u);
}

TEST_CASE("garbage on the wire does not kill the reader") {
    FakeLink link;
    LinkBody body(link.take_brain_fd());
    link.send_raw({0x00, 0x11, 0xA5, 0x99, 0x7F, 0x42});
    auto broken = wire::encode_json(json{{"event", "button.press"}});
    broken[7] ^= 0x40;           // fails the CRC
    link.send_raw(broken);
    std::mutex m;
    std::vector<std::string> seen;
    body.set_event_sink([&](const json& j) { std::lock_guard<std::mutex> g(m); seen.push_back(j.value("event", "")); });
    link.send_json(json{{"event", "button.hold"}});
    link.send_json(json{{"event", "button.hold"}});
    REQUIRE(FakeLink::wait_for([&] { std::lock_guard<std::mutex> g(m); return !seen.empty(); }));
    CHECK(seen[0] == "button.hold");
    CHECK(body.stats().rx_bad >= 1u);
}

TEST_CASE("a command written during speech lands between audio frames") {
    FakeLink link;
    LinkBody body(link.take_brain_fd());
    say_hello(link, body);
    std::thread t([&] { body.speak(std::vector<int16_t>(16000, 0)); });
    REQUIRE(link.wait_audio(20));
    CHECK(body.chirp("drop"));   // a chirp must not wait for the speech to end
    REQUIRE(link.wait_commands(1));
    CHECK(link.commands()[0]["name"] == "drop");
    t.join();
    CHECK(link.audio().size() == 63);
}

TEST_CASE("opening a device that is not there fails loudly") {
    CHECK_THROWS_AS(LinkBody("/dev/definitely-not-a-uart"), std::runtime_error);
}
