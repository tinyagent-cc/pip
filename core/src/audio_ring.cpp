#include "pip/audio_ring.hpp"
namespace pip {

size_t AudioRing::used() const {
    const size_t h = head_, t = tail_;
    return (h - t) & (CAP - 1);
}

size_t AudioRing::free() const { return CAP - 1 - used(); }

void AudioRing::clear() {
    tail_ = head_;
    chirp_len_ = chirp_pos_ = 0;
}

size_t AudioRing::write(const int16_t* s, size_t n) {
    if (!s) return 0;
    const size_t room = free();
    if (n > room) n = room;
    size_t h = head_;
    for (size_t i = 0; i < n; ++i) {
        buf_[h] = s[i];
        h = (h + 1) & (CAP - 1);
    }
    head_ = h;
    return n;
}

void AudioRing::preempt(Chirp c) {
    // Render first, then arm. chirp_pos_ = 0 with a stale length would play the tail of the
    // previous chirp for as long as the render takes.
    chirp_len_ = 0;
    chirp_pos_ = 0;
    chirp_len_ = render_chirp(c, chirp_, CHIRP_MAX_SAMPLES);
}

size_t AudioRing::pull(int16_t* out, size_t n) {
    if (!out) return 0;
    size_t t = tail_;
    const size_t h = head_;
    for (size_t i = 0; i < n; ++i) {
        int16_t speech = 0;
        if (t != h) { speech = buf_[t]; t = (t + 1) & (CAP - 1); }
        if (chirp_pos_ < chirp_len_) out[i] = chirp_[chirp_pos_++];   // the chirp wins, the speech still drains
        else out[i] = speech;
    }
    tail_ = t;
    return n;
}
}
