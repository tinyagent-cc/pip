#pragma once
#include <cstddef>
#include <cstdint>
// The wire between the body and its brain. Platform-free on purpose: the framing is
// host-tested here and the UART only moves bytes.
namespace pip::link {
constexpr uint8_t SYNC = 0xA5;
enum class Type : uint8_t { Json = 0x01, Audio = 0x02 };
constexpr size_t MAX_PAYLOAD = 512, HEADER = 4, MAX_FRAME = HEADER + MAX_PAYLOAD + 1;
// CRC-8 of the CRC catalogue: poly 0x07, MSB first, init 0, no reflection, no xorout.
// crc8("123456789", 9) == 0xF4.
uint8_t crc8(const uint8_t* p, size_t n, uint8_t crc = 0);
// Writes 0xA5 | type | len u16 LE | payload | crc8(type,len,payload). Returns bytes, or 0
// when the payload is over MAX_PAYLOAD or out is too small.
size_t encode(Type t, const uint8_t* payload, uint16_t len, uint8_t* out, size_t cap);
struct Frame { Type type; uint16_t len; const uint8_t* payload; };
// Byte-at-a-time receiver. Anything it cannot make sense of counts as bad and it goes
// hunting for the next SYNC, so a half-received frame costs one count, not the stream.
class Decoder {
public:
    bool push(uint8_t b);                          // true when frame() holds a complete, CRC-valid frame, until the next push
    const Frame& frame() const { return frame_; }
    uint32_t frames() const { return frames_; }    // good frames
    uint32_t bad() const { return bad_; }          // CRC failures, unknown types, oversize lengths
private:
    bool sync(uint8_t b);                          // the Sync-state check, also used to re-examine a rejected byte
    enum class St : uint8_t { Sync, Type, Len0, Len1, Payload, Crc } st_ = St::Sync;
    uint8_t buf_[MAX_PAYLOAD];
    uint16_t len_ = 0, got_ = 0;
    uint8_t type_ = 0, crc_ = 0;
    Frame frame_{};
    uint32_t frames_ = 0, bad_ = 0;
};
}
