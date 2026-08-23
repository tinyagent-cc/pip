#pragma once
#include <cstddef>
#include <cstdint>
#include "pip/chirps.hpp"
#include "pip/protocol.hpp"
namespace pip {
// Everything Pip is about to make a noise with, in one place: a single-producer /
// single-consumer ring of s16 mono samples at AUDIO_RATE for speech, plus a side buffer
// holding the chirp that is currently talking over it.
//
// On the Pico both ends live in the main loop (the link drain writes, the I2S fill pulls),
// so nothing here needs atomics; the volatile indices keep the compiler from caching them
// across the two halves of a frame. One slot stays empty so head == tail means empty.
//
// A chirp pre-empts speech: while it plays its samples go out instead, and the speech
// underneath is consumed at the same rate, so a sentence that gets interrupted stays in
// step with the real time the brain paced it at.
class AudioRing {
public:
    static constexpr size_t CAP = 32768;                 // samples, 64 KB, power of two
    size_t write(const int16_t* s, size_t n);            // appends up to free(); returns what it took
    size_t free() const;
    size_t used() const;
    void clear();                                        // drops the speech and stops the chirp
    void preempt(Chirp c);                               // renders c and starts it now, over anything already playing
    bool chirp_playing() const { return chirp_pos_ < chirp_len_; }
    bool speech_playing() const { return used() > 0; }
    bool playing() const { return chirp_playing() || speech_playing(); }
    // Writes exactly n output samples: the chirp while one plays (consuming speech
    // underneath), else speech, else silence. Always returns n.
    size_t pull(int16_t* out, size_t n);
private:
    int16_t buf_[CAP];
    volatile size_t head_ = 0, tail_ = 0;                // producer moves head_, consumer moves tail_
    int16_t chirp_[CHIRP_MAX_SAMPLES];
    size_t chirp_len_ = 0, chirp_pos_ = 0;
};
}
