#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "fake_body.hpp"
#include "fake_services.hpp"
#include "services.hpp"
#include "speaker.hpp"
#include <algorithm>
#include <chrono>
using namespace pip::brain;
using V = std::vector<std::string>;

namespace {
size_t count_prefix(const V& calls, const std::string& prefix) {
    return static_cast<size_t>(std::count_if(calls.begin(), calls.end(),
        [&](const std::string& s) { return s.rfind(prefix, 0) == 0; }));
}
}  // namespace

TEST_CASE("Cortex listens and sees against the fake Jetson") {
    FakeCortex cortex;
    Cortex c(cortex.url());
    CHECK(c.enabled());
    CHECK(c.ok());
    auto heard = c.listen(4);
    REQUIRE(heard.has_value());
    CHECK(heard->text == "what do you see");
    CHECK(heard->lang == "en");
    auto seen = c.see("who is there?");
    REQUIRE(seen.has_value());
    CHECK(*seen == "a desk with a keyboard");
    CHECK(cortex.snapshot() == V{"listen:{\"seconds\":4}", "see:who is there?"});
}

TEST_CASE("Cortex without a URL is disabled; a dead one fails fast and says why") {
    Cortex off("");
    CHECK_FALSE(off.enabled());
    CHECK_FALSE(off.ok());
    CHECK_FALSE(off.listen(4).has_value());

    Cortex dead("http://127.0.0.1:1");
    CHECK(dead.enabled());
    auto t0 = std::chrono::steady_clock::now();
    CHECK_FALSE(dead.listen(4).has_value());
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(ms < 2000);
    CHECK_FALSE(dead.last_error().empty());
}

TEST_CASE("Cortex reports a 500 as no answer, not as an empty answer") {
    FakeCortex cortex;
    cortex.fail = true;
    Cortex c(cortex.url());
    CHECK_FALSE(c.ok());
    CHECK_FALSE(c.listen(4).has_value());
    CHECK_FALSE(c.see("what?").has_value());
    CHECK(c.last_error().find("500") != std::string::npos);
}

TEST_CASE("Voice turns text into s16 PCM") {
    FakeVoice voice;
    Voice v(voice.url());
    CHECK(v.enabled());
    CHECK(v.ok());
    auto pcm = v.tts("hi");
    REQUIRE(pcm.has_value());
    CHECK(pcm->size() == 32u);           // the fake gives 16 samples per character
    CHECK_FALSE(Voice("").tts("hi").has_value());
}

TEST_CASE("Speaker shows the bubble first, then speaks it") {
    FakeBody body; FakeVoice voice; EventLog log{50, nullptr};
    Voice v(voice.url());
    Speaker sp(body, v, log);
    sp.say("hi");
    sp.wait_idle();
    CHECK(body.snapshot() == V{"say:hi", "speak:32"});
    CHECK(sp.last_voice_ok());
    CHECK_FALSE(sp.busy());
}

TEST_CASE("Speaker can show a line without speaking it") {
    FakeBody body; FakeVoice voice; EventLog log{50, nullptr};
    Voice v(voice.url());
    Speaker sp(body, v, log);
    sp.say("just the bubble", false);
    sp.wait_idle();
    CHECK(body.snapshot() == V{"say:just the bubble"});
    CHECK(voice.snapshot().empty());
}

TEST_CASE("the queue is bounded: a flood drops the oldest lines and says so") {
    FakeBody body; FakeVoice voice; EventLog log{50, nullptr};
    voice.delay_ms = 120;                // the voice is slower than the caller
    Voice v(voice.url());
    Speaker sp(body, v, log);
    for (int i = 0; i < 6; ++i) sp.say("line " + std::to_string(i));
    sp.wait_idle();
    auto calls = body.snapshot();
    CHECK(count_prefix(calls, "say:") == 6);          // every bubble showed
    CHECK(count_prefix(calls, "speak:") <= 5);        // one in flight plus a queue of four
    bool dropped = false;
    for (auto& e : log.tail(50))
        if (e["kind"] == "note" && e["detail"].get<std::string>().find("dropped") != std::string::npos) dropped = true;
    CHECK(dropped);
}

TEST_CASE("with the voice down the bubble still shows and the caller can tell") {
    FakeBody body; FakeVoice voice; EventLog log{50, nullptr};
    voice.fail = true;
    Voice v(voice.url());
    Speaker sp(body, v, log);
    sp.say("anyone listening?");
    sp.wait_idle();
    CHECK(body.snapshot() == V{"say:anyone listening?"});
    CHECK_FALSE(sp.last_voice_ok());
}

TEST_CASE("Speaker never blocks its caller on the voice") {
    FakeBody body; FakeVoice voice; EventLog log{50, nullptr};
    voice.delay_ms = 300;
    Voice v(voice.url());
    Speaker sp(body, v, log);
    auto t0 = std::chrono::steady_clock::now();
    sp.say("slow voice");
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(ms < 100);
    CHECK(sp.busy());
    sp.wait_idle();
    CHECK(count_prefix(body.snapshot(), "speak:") == 1);
}
