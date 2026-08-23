#include <cstdlib>
#include <cstring>
#include "check.h"
#include "pip/chirps.hpp"
#include "pip/protocol.hpp"

using namespace pip;

static int16_t buf[CHIRP_MAX_SAMPLES];
static int16_t buf2[CHIRP_MAX_SAMPLES];

static int peak_of(const int16_t* s, size_t n) {
    int p = 0;
    for (size_t i = 0; i < n; ++i) { int a = s[i] < 0 ? -s[i] : s[i]; if (a > p) p = a; }
    return p;
}

static void run() {
    for (uint8_t i = 0; i < (uint8_t)Chirp::Count; ++i) {
        Chirp c = (Chirp)i;
        std::memset(buf, 0x7F, sizeof buf);
        size_t n = render_chirp(c, buf, CHIRP_MAX_SAMPLES);
        // 150-600 ms at 16 kHz, and never past the caller's buffer.
        CHECK(n >= 2400);
        CHECK(n <= CHIRP_MAX_SAMPLES);
        int p = peak_of(buf, n);
        CHECK(p <= 20000);   // headroom under the s16 rail so the amp never clips
        CHECK(p >= 6000);    // and loud enough to hear across a desk
        // The envelope: no click on either edge.
        for (size_t k = 0; k < 5; ++k) CHECK(std::abs(buf[k]) < 600);
        for (size_t k = n - 5; k < n; ++k) CHECK(std::abs(buf[k]) < 600);
        // Deterministic: same chirp, same samples, every time.
        size_t n2 = render_chirp(c, buf2, CHIRP_MAX_SAMPLES);
        CHECK_EQ(n2, n);
        CHECK(std::memcmp(buf, buf2, n * sizeof(int16_t)) == 0);
    }

    // cap wins over the recipe length, and nothing is written past it
    std::memset(buf, 0, sizeof buf);
    CHECK_EQ(render_chirp(Chirp::Rise, buf, 100), (size_t)100);
    CHECK_EQ(buf[100], (int16_t)0);
    CHECK_EQ(render_chirp(Chirp::Boot, buf, 0), (size_t)0);

    // The two chirps Plan B adds are in the enum and in the name table.
    Chirp c = Chirp::Rise;
    CHECK(chirp_from("boot", c));
    CHECK(c == Chirp::Boot);
    CHECK(chirp_from("sad", c));
    CHECK(c == Chirp::Sad);
    CHECK_STREQ(chirp_name(Chirp::Sad), "sad");
    CHECK_STREQ(chirp_name(Chirp::Boot), "boot");
    CHECK(!chirp_from("bootx", c));

    // Different recipes really are different sounds.
    size_t na = render_chirp(Chirp::Rise, buf, CHIRP_MAX_SAMPLES);
    size_t nb = render_chirp(Chirp::Drop, buf2, CHIRP_MAX_SAMPLES);
    CHECK(na != nb || std::memcmp(buf, buf2, na * sizeof(int16_t)) != 0);
}

TEST_MAIN()
