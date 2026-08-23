#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "wire.hpp"
using namespace pip::brain::wire;
using pip::brain::wire::json;

namespace {
// Feeds every byte to the decoder and reports how many frames completed.
int feed(Decoder& d, const std::vector<uint8_t>& bytes) {
    int n = 0;
    for (uint8_t b : bytes) if (d.push(b)) ++n;
    return n;
}
}  // namespace

TEST_CASE("crc8 matches the check value the firmware computes") {
    const char* s = "123456789";
    CHECK(crc8(reinterpret_cast<const uint8_t*>(s), 9) == 0xF4);
    CHECK(crc8(nullptr, 0) == 0x00);
}

TEST_CASE("a JSON frame round trips") {
    json j{{"cmd", "express"}, {"emotion", "wink"}};
    auto bytes = encode_json(j);
    CHECK(bytes[0] == SYNC);
    CHECK(bytes[1] == 1);
    Decoder d;
    CHECK(feed(d, bytes) == 1);
    CHECK(d.frames() == 1u);
    CHECK(d.bad() == 0u);
    CHECK(d.frame().type == Type::Json);
    std::string payload(d.frame().payload.begin(), d.frame().payload.end());
    CHECK(json::parse(payload) == j);
}

TEST_CASE("a full 512-byte audio frame round trips; 513 bytes is refused") {
    std::vector<uint8_t> pcm(MAX_PAYLOAD);
    for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = static_cast<uint8_t>(i * 7 + 3);
    auto bytes = encode(Type::Audio, pcm.data(), pcm.size());
    CHECK(bytes.size() == MAX_PAYLOAD + 5);
    Decoder d;
    CHECK(feed(d, bytes) == 1);
    CHECK(d.frame().type == Type::Audio);
    CHECK(d.frame().payload == pcm);

    std::vector<uint8_t> too_big(MAX_PAYLOAD + 1, 0);
    CHECK_THROWS_AS(encode(Type::Audio, too_big.data(), too_big.size()), std::length_error);
}

TEST_CASE("an empty payload frame round trips") {
    Decoder d;
    auto bytes = encode(Type::Json, nullptr, 0);
    CHECK(feed(d, bytes) == 1);
    CHECK(d.frame().payload.empty());
    CHECK(d.bad() == 0u);
}

TEST_CASE("garbage before a frame is counted once and skipped") {
    Decoder d;
    for (uint8_t b : {uint8_t(0x00), uint8_t(0x11), uint8_t(0x22), uint8_t(0x33)}) CHECK_FALSE(d.push(b));
    auto bytes = encode_json(json{{"pong", true}});
    CHECK(feed(d, bytes) == 1);
    CHECK(d.frames() == 1u);
    CHECK(d.bad() >= 1u);
}

TEST_CASE("a corrupted payload byte costs one frame, the next one still decodes") {
    auto bad_frame = encode_json(json{{"event", "button.press"}});
    bad_frame[6] ^= 0x01;   // inside the payload, so the CRC no longer matches
    auto good = encode_json(json{{"event", "button.hold"}});
    Decoder d;
    CHECK(feed(d, bad_frame) == 0);
    CHECK(d.bad() == 1u);
    CHECK(feed(d, good) == 1);
    CHECK(d.frames() == 1u);
    CHECK(d.bad() == 1u);
    std::string payload(d.frame().payload.begin(), d.frame().payload.end());
    CHECK(json::parse(payload)["event"] == "button.hold");
}

TEST_CASE("an unknown type byte or an oversized length resyncs instead of hanging") {
    Decoder d;
    for (uint8_t b : {SYNC, uint8_t(0x09)}) d.push(b);           // type 9 is not on the wire
    CHECK(d.bad() == 1u);
    for (uint8_t b : {SYNC, uint8_t(0x01), uint8_t(0x01), uint8_t(0x02)}) d.push(b);  // len 0x0201 = 513
    CHECK(d.bad() == 2u);
    auto good = encode_json(json{{"pong", true}});
    CHECK(feed(d, good) == 1);
    CHECK(d.frames() == 1u);
}

TEST_CASE("a stray 0xA5 inside a garbage run does not wedge the decoder") {
    Decoder d;
    for (uint8_t b : {uint8_t(0x7F), SYNC, uint8_t(0x01), uint8_t(0xFF)}) d.push(b);   // looks like a frame header, is not
    auto good = encode_json(json{{"hello", {{"fw", "v1"}, {"protocol", 1}}}});
    // The bogus header claims 0xFF+ bytes of payload, so it eats the real frame
    // that follows; the frame after that decodes. Two frames in a row is what a
    // real link sends, and the decoder must catch up within one of them.
    feed(d, good);
    CHECK(feed(d, good) == 1);
    CHECK(d.frames() == 1u);
}
