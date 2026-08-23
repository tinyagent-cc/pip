#include <cstring>
#include <vector>
#include "check.h"
#include "pip/audio_ring.hpp"
#include "pip/chirps.hpp"

using namespace pip;

static AudioRing g_ring;                 // 64 KB plus the chirp buffer: too big for the stack
static int16_t g_chirp[CHIRP_MAX_SAMPLES];

static void run() {
    AudioRing& r = g_ring;
    std::vector<int16_t> in(1000);
    for (size_t i = 0; i < in.size(); ++i) in[i] = (int16_t)((int)i - 500);

    // Plain speech in, plain speech out, in order.
    CHECK_EQ(r.write(in.data(), in.size()), (size_t)1000);
    CHECK_EQ(r.used(), (size_t)1000);
    CHECK(r.speech_playing());
    CHECK(r.playing());
    CHECK(!r.chirp_playing());
    int16_t out[512];
    CHECK_EQ(r.pull(out, 400), (size_t)400);
    for (size_t i = 0; i < 400; ++i) CHECK_EQ(out[i], in[i]);
    CHECK_EQ(r.used(), (size_t)600);

    // A full ring truncates and says how much it took.
    r.clear();
    CHECK_EQ(r.used(), (size_t)0);
    CHECK(!r.playing());
    std::vector<int16_t> big(AudioRing::CAP, 7);
    const size_t free_before = r.free();
    CHECK(free_before >= AudioRing::CAP - 1);
    CHECK_EQ(r.write(big.data(), big.size()), free_before);
    CHECK_EQ(r.free(), (size_t)0);
    CHECK_EQ(r.write(big.data(), 10), (size_t)0);
    CHECK_EQ(r.used(), free_before);

    // A chirp replaces speech for its length, and the speech underneath still drains, so
    // whatever the brain is streaming stays aligned to real time.
    r.clear();
    CHECK_EQ(r.write(in.data(), in.size()), (size_t)1000);
    r.preempt(Chirp::Rise);
    CHECK(r.chirp_playing());
    CHECK(r.playing());
    const size_t rise_n = render_chirp(Chirp::Rise, g_chirp, CHIRP_MAX_SAMPLES);
    CHECK(rise_n > 100);
    CHECK_EQ(r.pull(out, 100), (size_t)100);
    for (size_t i = 0; i < 100; ++i) CHECK_EQ(out[i], g_chirp[i]);
    CHECK_EQ(r.used(), (size_t)900);          // speech consumed underneath

    // Once the chirp runs out, speech comes back where it got to.
    r.clear();
    const size_t drop_n = render_chirp(Chirp::Drop, g_chirp, CHIRP_MAX_SAMPLES);
    std::vector<int16_t> speech(drop_n + 200);
    for (size_t i = 0; i < speech.size(); ++i) speech[i] = (int16_t)(1000 + (i % 3000));
    CHECK_EQ(r.write(speech.data(), speech.size()), speech.size());
    r.preempt(Chirp::Drop);
    std::vector<int16_t> all(drop_n + 200);
    CHECK_EQ(r.pull(all.data(), all.size()), all.size());
    CHECK(!r.chirp_playing());
    for (size_t i = 0; i < drop_n; ++i) CHECK_EQ(all[i], g_chirp[i]);
    for (size_t i = 0; i < 200; ++i) CHECK_EQ(all[drop_n + i], speech[drop_n + i]);
    CHECK_EQ(r.used(), (size_t)0);

    // A chirp arriving over a playing one restarts from the top of the new one.
    r.clear();
    r.preempt(Chirp::Rise);
    r.pull(out, 200);
    r.preempt(Chirp::Trill);
    const size_t trill_n = render_chirp(Chirp::Trill, g_chirp, CHIRP_MAX_SAMPLES);
    CHECK(trill_n > 50);
    r.pull(out, 50);
    for (size_t i = 0; i < 50; ++i) CHECK_EQ(out[i], g_chirp[i]);

    // Nothing to play is silence, not stale samples.
    r.clear();
    CHECK(!r.chirp_playing());
    CHECK(!r.playing());
    std::memset(out, 0x55, sizeof out);
    CHECK_EQ(r.pull(out, 512), (size_t)512);
    for (size_t i = 0; i < 512; ++i) CHECK_EQ(out[i], (int16_t)0);

    // Speech shorter than the pull: the rest is silence, not a repeat.
    CHECK_EQ(r.write(in.data(), 10), (size_t)10);
    CHECK_EQ(r.pull(out, 64), (size_t)64);
    for (size_t i = 0; i < 10; ++i) CHECK_EQ(out[i], in[i]);
    for (size_t i = 10; i < 64; ++i) CHECK_EQ(out[i], (int16_t)0);

    // Wrap: write and read past the end of the buffer, order still holds.
    r.clear();
    std::vector<int16_t> ramp(4096), back(4096);
    for (size_t i = 0; i < ramp.size(); ++i) ramp[i] = (int16_t)(i & 0x7FFF);
    for (int round = 0; round < 12; ++round) {          // 12 * 4096 > CAP, so it wraps
        CHECK_EQ(r.write(ramp.data(), ramp.size()), ramp.size());
        CHECK_EQ(r.pull(back.data(), back.size()), back.size());
        CHECK(std::memcmp(ramp.data(), back.data(), ramp.size() * sizeof(int16_t)) == 0);
    }
    CHECK_EQ(r.used(), (size_t)0);
}

TEST_MAIN()
