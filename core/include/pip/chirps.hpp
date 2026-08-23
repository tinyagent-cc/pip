#pragma once
#include <cstddef>
#include <cstdint>
#include "pip/protocol.hpp"
// Pip's voice before it has words. Six short sounds, synthesized on demand from sine
// segments and a linear ADSR envelope; no audio files, no tables in flash.
namespace pip {
constexpr int AUDIO_RATE = 16000;             // s16 mono everywhere in Pip
constexpr size_t CHIRP_MAX_SAMPLES = 9600;    // 600 ms, the longest recipe v1 allows
// Renders chirp c into out (mono s16 at AUDIO_RATE). Returns the sample count, which is
// min(recipe length, cap) and never more than CHIRP_MAX_SAMPLES. Deterministic: the same
// chirp gives the same samples on the host and on the Pico.
size_t render_chirp(Chirp c, int16_t* out, size_t cap);
}
