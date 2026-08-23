#pragma once
#include <nlohmann/json.hpp>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

// The link the brain and the body share over UART:
//
//   0xA5 | type u8 | len u16 LE | payload | crc8(type, len_lo, len_hi, payload)
//
// Header-only and dependency-light on purpose: the firmware carries the same
// codec in core/, and a later cleanup may unify the two. Until then the two
// copies are held together by the check values in the tests (crc of
// "123456789" == 0xF4), which both sides assert.
namespace pip::brain::wire {
using json = nlohmann::json;

constexpr uint8_t SYNC = 0xA5;
enum class Type : uint8_t { Json = 1, Audio = 2 };
constexpr size_t MAX_PAYLOAD = 512;   // one audio frame: 256 s16le samples

// CRC-8, polynomial 0x07, MSB-first, init 0, no final xor.
inline uint8_t crc8(const uint8_t* p, size_t n, uint8_t crc = 0) {
    for (size_t i = 0; i < n; ++i) {
        crc ^= p[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x07) : static_cast<uint8_t>(crc << 1);
    }
    return crc;
}

inline std::vector<uint8_t> encode(Type t, const uint8_t* payload, size_t len) {
    if (len > MAX_PAYLOAD)
        throw std::length_error("pip wire: payload " + std::to_string(len) + " bytes > " + std::to_string(MAX_PAYLOAD));
    std::vector<uint8_t> out;
    out.reserve(len + 5);
    out.push_back(SYNC);
    out.push_back(static_cast<uint8_t>(t));
    out.push_back(static_cast<uint8_t>(len & 0xFF));
    out.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    if (len) out.insert(out.end(), payload, payload + len);   // payload may be null when len == 0
    out.push_back(crc8(out.data() + 1, len + 3));
    return out;
}

inline std::vector<uint8_t> encode_json(const json& j) {
    std::string s = j.dump();
    return encode(Type::Json, reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

struct Frame {
    Type type = Type::Json;
    std::vector<uint8_t> payload;
};

// Byte-at-a-time decoder: push() returns true exactly when frame() holds a
// new, CRC-checked frame. Anything else on the wire (line noise, a half
// frame left over from a reboot) resyncs on the next 0xA5. bad() counts
// framing failures, one per rejected frame and one per contiguous run of
// discarded bytes, so a stream of noise is one count, not thousands.
class Decoder {
public:
    bool push(uint8_t b) {
        switch (st_) {
            case S::Sync:
                if (b == SYNC) { st_ = S::Type; hunting_ = false; }
                else if (!hunting_) { ++bad_; hunting_ = true; }
                return false;
            case S::Type:
                if (b != static_cast<uint8_t>(Type::Json) && b != static_cast<uint8_t>(Type::Audio)) {
                    ++bad_;
                    // A 0xA5 here is far more likely to be the real sync than a
                    // type byte, so hold the state instead of dropping it.
                    st_ = (b == SYNC) ? S::Type : S::Sync;
                    hunting_ = (b != SYNC);
                    return false;
                }
                type_ = b;
                run_ = crc8(&b, 1, 0);
                st_ = S::LenLo;
                return false;
            case S::LenLo:
                len_ = b;
                run_ = crc8(&b, 1, run_);
                st_ = S::LenHi;
                return false;
            case S::LenHi:
                len_ = static_cast<uint16_t>(len_ | (static_cast<uint16_t>(b) << 8));
                run_ = crc8(&b, 1, run_);
                if (len_ > MAX_PAYLOAD) { ++bad_; st_ = S::Sync; hunting_ = false; return false; }
                buf_.clear();
                buf_.reserve(len_);
                st_ = len_ ? S::Payload : S::Crc;
                return false;
            case S::Payload:
                buf_.push_back(b);
                run_ = crc8(&b, 1, run_);
                if (buf_.size() >= len_) st_ = S::Crc;
                return false;
            case S::Crc:
            default:
                st_ = S::Sync;
                hunting_ = false;
                if (b != run_) { ++bad_; return false; }
                frame_.type = static_cast<Type>(type_);
                frame_.payload = std::move(buf_);
                ++frames_;
                return true;
        }
    }
    const Frame& frame() const { return frame_; }
    uint32_t frames() const { return frames_; }
    uint32_t bad() const { return bad_; }

private:
    enum class S { Sync, Type, LenLo, LenHi, Payload, Crc };
    S st_ = S::Sync;
    Frame frame_;
    std::vector<uint8_t> buf_;
    uint16_t len_ = 0;
    uint8_t type_ = 0, run_ = 0;
    bool hunting_ = false;
    uint32_t frames_ = 0, bad_ = 0;
};

}  // namespace pip::brain::wire
