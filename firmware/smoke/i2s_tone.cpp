#include <cmath>
#include <cstdio>
#include "pico/stdlib.h"
#include "pico/audio_i2s.h"

// Two seconds of 440 Hz, then two of silence, forever. Stereo S16 at 44.1 kHz, the same
// shape as pico-playground's sine_wave example. If this sings on RP2350, Plan 3b is a go.
int main() {
    stdio_init_all();
    sleep_ms(1500); // USB CDC DTR gate: give the host time to open the port before the first print
    static audio_format_t fmt = {44100, AUDIO_BUFFER_FORMAT_PCM_S16, 2};
    static audio_buffer_format_t bfmt = {&fmt, 4};
    audio_buffer_pool_t* pool = audio_new_producer_pool(&bfmt, 3, 256);
    audio_i2s_config_t cfg = {PICO_AUDIO_I2S_DATA_PIN, PICO_AUDIO_I2S_CLOCK_PIN_BASE, 0, 0};
    const audio_format_t* out = audio_i2s_setup(&fmt, &cfg);
    if (!out) { printf("i2s: setup failed\n"); return 1; }
    if (!audio_i2s_connect(pool)) { printf("i2s: connect failed\n"); return 1; }
    audio_i2s_set_enabled(true);
    printf("i2s: running, 440 Hz bursts on DIN=%d BCLK=%d LRCLK=%d\n", PICO_AUDIO_I2S_DATA_PIN, PICO_AUDIO_I2S_CLOCK_PIN_BASE, PICO_AUDIO_I2S_CLOCK_PIN_BASE + 1);
    uint32_t phase = 0, n = 0;
    bool was_on = false;
    const uint32_t step = (uint32_t)(440.0 * 4294967296.0 / 44100.0);
    while (true) {
        audio_buffer_t* b = take_audio_buffer(pool, true);
        int16_t* s = (int16_t*)b->buffer->bytes;
        bool on = ((n / 44100) % 4) < 2;
        // repeat the line on every tone-on transition (every 4 s) so a serial capture
        // that misses the boot print still catches it
        if (on && !was_on) {
            printf("i2s: running, 440 Hz bursts on DIN=%d BCLK=%d LRCLK=%d\n", PICO_AUDIO_I2S_DATA_PIN, PICO_AUDIO_I2S_CLOCK_PIN_BASE, PICO_AUDIO_I2S_CLOCK_PIN_BASE + 1);
        }
        was_on = on;
        for (uint i = 0; i < b->max_sample_count; ++i, ++n) {
            phase += step;
            int16_t v = on ? (int16_t)(std::sin(phase * (6.283185307 / 4294967296.0)) * 6000) : 0;
            s[2 * i] = v; s[2 * i + 1] = v;
        }
        b->sample_count = b->max_sample_count;
        give_audio_buffer(pool, b);
    }
}
