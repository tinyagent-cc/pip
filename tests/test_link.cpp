#include <cstring>
#include "check.h"
#include "pip/link.hpp"
using namespace pip;
using namespace pip::link;

// Feeds every byte of buf through d and returns how many complete frames came out; the
// last one stays readable through d.frame().
static int feed(Decoder& d, const uint8_t* buf, size_t n, uint8_t* last_payload = nullptr, uint16_t* last_len = nullptr) {
    int got = 0;
    for (size_t i = 0; i < n; ++i)
        if (d.push(buf[i])) {
            ++got;
            if (last_payload) std::memcpy(last_payload, d.frame().payload, d.frame().len);
            if (last_len) *last_len = d.frame().len;
        }
    return got;
}

static void run() {
    CHECK_EQ(crc8((const uint8_t*)"123456789", 9), (uint8_t)0xF4);
    CHECK_EQ(crc8(nullptr, 0), (uint8_t)0x00);

    // JSON round trip.
    const char* js = "{\"cmd\":\"express\",\"emotion\":\"wink\"}";
    uint8_t out[MAX_FRAME];
    size_t n = encode(Type::Json, (const uint8_t*)js, (uint16_t)std::strlen(js), out, sizeof out);
    CHECK_EQ(n, HEADER + std::strlen(js) + 1);
    CHECK_EQ(out[0], SYNC); CHECK_EQ(out[1], (uint8_t)0x01);
    CHECK_EQ(out[2], (uint8_t)(std::strlen(js) & 0xFF)); CHECK_EQ(out[3], (uint8_t)0x00);   // len is little endian
    Decoder d;
    uint8_t pay[MAX_PAYLOAD]; uint16_t plen = 0;
    CHECK_EQ(feed(d, out, n, pay, &plen), 1);
    CHECK_EQ((int)plen, (int)std::strlen(js));
    CHECK_EQ(std::memcmp(pay, js, plen), 0);
    CHECK(d.frame().type == Type::Json);
    CHECK_EQ(d.frames(), 1u); CHECK_EQ(d.bad(), 0u);

    // A full 512-byte audio frame, the largest the wire carries.
    uint8_t audio[MAX_PAYLOAD];
    for (size_t i = 0; i < MAX_PAYLOAD; ++i) audio[i] = (uint8_t)(i * 7 + 3);
    n = encode(Type::Audio, audio, (uint16_t)MAX_PAYLOAD, out, sizeof out);
    CHECK_EQ(n, MAX_FRAME);
    CHECK_EQ(out[2], (uint8_t)0x00); CHECK_EQ(out[3], (uint8_t)0x02);   // 512 = 0x0200 LE
    Decoder d2;
    CHECK_EQ(feed(d2, out, n, pay, &plen), 1);
    CHECK_EQ((int)plen, (int)MAX_PAYLOAD);
    CHECK_EQ(std::memcmp(pay, audio, MAX_PAYLOAD), 0);
    CHECK(d2.frame().type == Type::Audio);

    // Oversize payload and a too-small output buffer both refuse rather than truncate.
    CHECK_EQ(encode(Type::Json, audio, (uint16_t)(MAX_PAYLOAD + 1), out, sizeof out), (size_t)0);
    CHECK_EQ(encode(Type::Json, (const uint8_t*)js, 4, out, 8), (size_t)0);

    // Garbage before a good frame: 0xA5 0x09 looks like a frame with an unknown type, which
    // costs one bad count, and the good frame right behind it still decodes.
    Decoder d3;
    const uint8_t junk[] = {0x00, 0xFF, 0xA5, 0x09};
    CHECK_EQ(feed(d3, junk, sizeof junk), 0);
    n = encode(Type::Json, (const uint8_t*)js, (uint16_t)std::strlen(js), out, sizeof out);
    CHECK_EQ(feed(d3, out, n, pay, &plen), 1);
    CHECK_EQ(d3.bad(), 1u); CHECK_EQ(d3.frames(), 1u);
    CHECK_EQ(std::memcmp(pay, js, plen), 0);

    // A corrupted payload byte fails the CRC; the next good frame is unaffected.
    Decoder d4;
    uint8_t bad[MAX_FRAME];
    std::memcpy(bad, out, n);
    bad[HEADER + 2] ^= 0x20;
    CHECK_EQ(feed(d4, bad, n), 0);
    CHECK_EQ(d4.bad(), 1u); CHECK_EQ(d4.frames(), 0u);
    CHECK_EQ(feed(d4, out, n, pay, &plen), 1);
    CHECK_EQ(d4.frames(), 1u); CHECK_EQ(d4.bad(), 1u);

    // Back to back, no gap.
    Decoder d5;
    uint8_t two[2 * MAX_FRAME];
    std::memcpy(two, out, n);
    size_t n2 = encode(Type::Json, (const uint8_t*)"{\"cmd\":\"ping\"}", 14, two + n, sizeof two - n);
    CHECK(n2 > 0);
    CHECK_EQ(feed(d5, two, n + n2, pay, &plen), 2);
    CHECK_EQ((int)plen, 14);
    CHECK_EQ(std::memcmp(pay, "{\"cmd\":\"ping\"}", 14), 0);
    CHECK_EQ(d5.frames(), 2u); CHECK_EQ(d5.bad(), 0u);

    // An over-long length field is refused before any payload is buffered, and the decoder
    // recovers on the next frame.
    Decoder d6;
    const uint8_t huge[] = {SYNC, 0x01, 0x01, 0x02};   // 513
    CHECK_EQ(feed(d6, huge, sizeof huge), 0);
    CHECK_EQ(d6.bad(), 1u);
    CHECK_EQ(feed(d6, out, n), 1);

    // A zero-length JSON frame is legal on the wire even though nothing sends one.
    Decoder d7;
    n = encode(Type::Json, nullptr, 0, out, sizeof out);
    CHECK_EQ(n, HEADER + 1);
    CHECK_EQ(feed(d7, out, n), 1);
    CHECK_EQ((int)d7.frame().len, 0);
}
TEST_MAIN()
