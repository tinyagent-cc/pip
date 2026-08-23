#include "pip/link.hpp"
#include <cstring>
namespace pip::link {
uint8_t crc8(const uint8_t* p, size_t n, uint8_t crc) {
    for (size_t i = 0; i < n; ++i) {
        crc ^= p[i];
        for (int b = 0; b < 8; ++b) crc = (uint8_t)((crc & 0x80) ? ((crc << 1) ^ 0x07) : (crc << 1));
    }
    return crc;
}
size_t encode(Type t, const uint8_t* payload, uint16_t len, uint8_t* out, size_t cap) {
    if (len > MAX_PAYLOAD) return 0;
    size_t need = HEADER + len + 1;
    if (cap < need) return 0;
    out[0] = SYNC;
    out[1] = (uint8_t)t;
    out[2] = (uint8_t)(len & 0xFF);
    out[3] = (uint8_t)(len >> 8);
    if (len) std::memcpy(out + HEADER, payload, len);
    out[need - 1] = crc8(out + 1, HEADER - 1 + len);   // type, len_lo, len_hi, payload
    return need;
}
bool Decoder::sync(uint8_t b) {
    if (b != SYNC) return false;
    st_ = St::Type;
    return true;
}
bool Decoder::push(uint8_t b) {
    switch (st_) {
        case St::Sync:
            sync(b);
            return false;
        case St::Type:
            if (b != (uint8_t)Type::Json && b != (uint8_t)Type::Audio) {
                // Not a frame after all. Count it and re-examine this byte as a possible
                // SYNC, so a stray 0xA5 in noise costs one frame, not the next real one.
                ++bad_;
                st_ = St::Sync;
                sync(b);
                return false;
            }
            type_ = b;
            crc_ = crc8(&b, 1, 0);
            st_ = St::Len0;
            return false;
        case St::Len0:
            len_ = b;
            crc_ = crc8(&b, 1, crc_);
            st_ = St::Len1;
            return false;
        case St::Len1:
            len_ = (uint16_t)(len_ | ((uint16_t)b << 8));
            if (len_ > MAX_PAYLOAD) { ++bad_; st_ = St::Sync; sync(b); return false; }
            crc_ = crc8(&b, 1, crc_);
            got_ = 0;
            st_ = len_ ? St::Payload : St::Crc;
            return false;
        case St::Payload:
            buf_[got_++] = b;
            crc_ = crc8(&b, 1, crc_);
            if (got_ == len_) st_ = St::Crc;
            return false;
        case St::Crc:
        default:
            st_ = St::Sync;
            if (b != crc_) { ++bad_; return false; }
            frame_ = Frame{(Type)type_, len_, buf_};
            ++frames_;
            return true;
    }
}
}
