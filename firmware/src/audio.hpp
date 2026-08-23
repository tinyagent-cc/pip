#pragma once
#include "pip/audio_ring.hpp"
#include "pip/protocol.hpp"
// Pip's speaker. The format is s16 mono at pip::AUDIO_RATE (16 kHz) everywhere inside Pip:
// chirps render it, the link carries it, the ring holds it. Only the last step is stereo,
// because pico_audio_i2s wants sample pairs and the MAX98357A takes one channel; both
// channels carry the same sample.
//
// Everything here runs on the main loop: link frames and chirps write the ring, pump()
// reads it. There is no IRQ producer and no IRQ consumer, so nothing needs locking.
namespace pip::audio {
// Brings up pico_audio_i2s at 16 kHz stereo S16 with a pool of 4 x 512-frame buffers
// (128 ms buffered ahead). False means no sound this boot; the caller carries on without it.
bool init();
// Fills every buffer the I2S driver has handed back, from the ring; silence when the ring
// is empty. Never blocks. Call at least once per frame.
void pump();
AudioRing& ring();
// What /senses reports: free ring space in bytes, and whether anything is queued to play.
AudioStats stats();
}
