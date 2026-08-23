#include "audio.hpp"
#include <cstdio>
#include "pico/audio_i2s.h"
#include "pins.hpp"

// pico_audio_i2s takes its pins as compile definitions, so CMake has to carry the numbers.
// pins.hpp stays the single source: drift between the two stops the build right here.
static_assert(PICO_AUDIO_I2S_DATA_PIN == (int)pip::pins::I2S_DIN, "I2S data pin drifted from pins.hpp");
static_assert(PICO_AUDIO_I2S_CLOCK_PIN_BASE == (int)pip::pins::I2S_BCLK, "I2S clock base drifted from pins.hpp");
static_assert(PICO_AUDIO_I2S_CLOCK_PIN_BASE + 1 == (int)pip::pins::I2S_LRCLK, "LRCLK is BCLK+1 in hardware");

namespace pip::audio {
namespace {
constexpr size_t BUF_SAMPLES = 512;          // per I2S buffer, 32 ms at 16 kHz
constexpr size_t BUF_COUNT = 4;              // 128 ms buffered ahead of the frame loop
AudioRing g_ring;                            // 64 KB of speech plus the chirp side buffer
audio_buffer_pool_t* g_pool = nullptr;
int16_t g_mono[BUF_SAMPLES];                 // one buffer's worth, before it is doubled to stereo
// How many of the buffers still queued for the DMA were filled from a non-empty ring.
// A sender pacing at real time leaves the ring empty most of the time, so "is Pip making a
// noise" is a question about the pipeline, not about the ring; this counts it down.
unsigned g_sound_buffers = 0;
}

AudioRing& ring() { return g_ring; }

bool init() {
    static audio_format_t fmt = {(uint32_t)AUDIO_RATE, AUDIO_BUFFER_FORMAT_PCM_S16, 2};
    static audio_buffer_format_t bfmt = {&fmt, 4};      // 4 bytes per stereo sample pair
    audio_buffer_pool_t* pool = audio_new_producer_pool(&bfmt, BUF_COUNT, BUF_SAMPLES);
    if (!pool) { printf("pip: audio pool failed\n"); return false; }
    audio_i2s_config_t cfg = {PICO_AUDIO_I2S_DATA_PIN, PICO_AUDIO_I2S_CLOCK_PIN_BASE, 0, 0};
    const audio_format_t* out = audio_i2s_setup(&fmt, &cfg);
    if (!out) { printf("pip: audio i2s setup failed\n"); return false; }
    if (!audio_i2s_connect(pool)) { printf("pip: audio i2s connect failed\n"); return false; }
    audio_i2s_set_enabled(true);
    g_pool = pool;
    printf("pip: audio %lu Hz s16 on DIN=%d BCLK=%d LRCLK=%d\n", (unsigned long)out->sample_freq,
           PICO_AUDIO_I2S_DATA_PIN, PICO_AUDIO_I2S_CLOCK_PIN_BASE, PICO_AUDIO_I2S_CLOCK_PIN_BASE + 1);
    return true;
}

void pump() {
    if (!g_pool) return;
    // Non-blocking take: the loop ends when the driver has nothing free, which is the whole
    // point. Blocking here would stall the face for up to 32 ms.
    while (audio_buffer_t* b = take_audio_buffer(g_pool, false)) {
        size_t n = b->max_sample_count;
        if (n > BUF_SAMPLES) n = BUF_SAMPLES;
        if (g_ring.playing()) g_sound_buffers = BUF_COUNT;
        else if (g_sound_buffers) --g_sound_buffers;
        g_ring.pull(g_mono, n);
        int16_t* s = (int16_t*)b->buffer->bytes;
        for (size_t i = 0; i < n; ++i) { s[2 * i] = g_mono[i]; s[2 * i + 1] = g_mono[i]; }
        b->sample_count = (uint32_t)n;
        give_audio_buffer(g_pool, b);
    }
}

AudioStats stats() {
    // playing means the speaker is making a noise, which stays true while the last of it is
    // still on its way through the I2S buffers.
    return AudioStats{(uint32_t)(g_ring.free() * sizeof(int16_t)), g_ring.playing() || g_sound_buffers > 0};
}
}
