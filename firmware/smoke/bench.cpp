#include <cmath>
#include <cstdio>
#include "pico/stdlib.h"
#include "pico/audio_i2s.h"
#include "hardware/uart.h"
#include "pins.hpp"

// Bench check for two wires at once: 440 Hz bursts on the I2S amp (same shape as i2s_tone)
// and a UART0 echo on GP0/GP1 at the link baud. Anything the Zero sends on /dev/ttyAMA0
// comes back prefixed "pico: " and is also printed on USB CDC.
static_assert(PICO_AUDIO_I2S_DATA_PIN == (int)pip::pins::I2S_DIN, "I2S data pin drifted from pins.hpp");
static_assert(PICO_AUDIO_I2S_CLOCK_PIN_BASE == (int)pip::pins::I2S_BCLK, "I2S clock base drifted from pins.hpp");
static_assert(PICO_AUDIO_I2S_CLOCK_PIN_BASE + 1 == (int)pip::pins::I2S_LRCLK, "LRCLK is BCLK+1 in hardware");
static_assert(pip::pins::UART_TX == 0 && pip::pins::UART_RX == 1, "bench assumes uart0 on GP0/GP1");

static constexpr unsigned LINK_BAUD = 921600;

int main() {
    stdio_init_all();
    sleep_ms(1500);
    uart_init(uart0, LINK_BAUD);
    gpio_set_function(pip::pins::UART_TX, GPIO_FUNC_UART);
    gpio_set_function(pip::pins::UART_RX, GPIO_FUNC_UART);
    uart_set_format(uart0, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(uart0, true);
    uart_puts(uart0, "pico bench up\r\n");

    static audio_format_t fmt = {44100, AUDIO_BUFFER_FORMAT_PCM_S16, 2};
    static audio_buffer_format_t bfmt = {&fmt, 4};
    audio_buffer_pool_t* pool = audio_new_producer_pool(&bfmt, 3, 256);
    audio_i2s_config_t cfg = {PICO_AUDIO_I2S_DATA_PIN, PICO_AUDIO_I2S_CLOCK_PIN_BASE, 0, 0};
    const audio_format_t* out = audio_i2s_setup(&fmt, &cfg);
    if (!out) { printf("i2s: setup failed\n"); return 1; }
    if (!audio_i2s_connect(pool)) { printf("i2s: connect failed\n"); return 1; }
    audio_i2s_set_enabled(true);
    printf("bench: tone bursts + uart0 echo at %u on GP%u/GP%u\n", LINK_BAUD, pip::pins::UART_TX, pip::pins::UART_RX);

    uint32_t phase = 0, n = 0;
    bool was_on = false;
    const uint32_t step = (uint32_t)(440.0 * 4294967296.0 / 44100.0);
    char line[128]; unsigned len = 0;
    while (true) {
        while (uart_is_readable(uart0)) {
            char c = (char)uart_getc(uart0);
            if (c == '\n' || c == '\r' || len == sizeof(line) - 1) {
                if (len) {
                    line[len] = 0;
                    uart_puts(uart0, "pico: "); uart_puts(uart0, line); uart_puts(uart0, "\r\n");
                    printf("uart rx: %s\n", line);
                    len = 0;
                }
            } else {
                line[len++] = c;
            }
        }
        audio_buffer_t* b = take_audio_buffer(pool, true);
        int16_t* s = (int16_t*)b->buffer->bytes;
        bool on = ((n / 44100) % 4) < 2;
        if (on && !was_on) printf("tone: on\n");
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
