#include "net/link.hpp"
#include <cstring>
#include "hardware/irq.h"
#include "hardware/uart.h"
#include "pico/stdlib.h"
#include "pins.hpp"
#include "pip/json_mini.hpp"
#include "pip/link.hpp"
namespace pip::net {
namespace {
// uart0 is a macro that casts a fixed address, so it is not a constant expression.
uart_inst_t* const UART = uart0;
constexpr unsigned UART_IRQ_NUM = UART0_IRQ;
constexpr size_t RING = 4096;                     // ~45 ms of wire at 921600 baud
uint8_t g_ring[RING];
volatile uint16_t g_head = 0, g_tail = 0;         // producer: the IRQ; consumer: the main loop
link::Decoder g_dec;
uint32_t g_last_good_ms = 0, g_audio_dropped = 0;
bool g_have_frame = false;

void on_uart_rx() {
    while (uart_is_readable(UART)) {
        uint8_t b = (uint8_t)uart_getc(UART);
        uint16_t next = (uint16_t)((g_head + 1) % RING);
        // A full ring means the main loop fell behind. Dropping the byte corrupts the frame
        // in flight, which the CRC catches and counts; the alternative is blocking in an IRQ.
        if (next == g_tail) continue;
        g_ring[g_head] = b;
        g_head = next;
    }
}
}

void link_init(unsigned baud) {
    uart_init(UART, baud);
    gpio_set_function(pins::UART_TX, GPIO_FUNC_UART);
    gpio_set_function(pins::UART_RX, GPIO_FUNC_UART);
    uart_set_hw_flow(UART, false, false);
    uart_set_format(UART, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(UART, true);
    irq_set_exclusive_handler(UART_IRQ_NUM, on_uart_rx);
    irq_set_enabled(UART_IRQ_NUM, true);
    uart_set_irq_enables(UART, true, false);      // RX only; TX is written blocking
}

bool link_send_json(const char* json) {
    size_t n = std::strlen(json);
    if (n > link::MAX_PAYLOAD) return false;
    static uint8_t out[link::MAX_FRAME];
    size_t len = link::encode(link::Type::Json, (const uint8_t*)json, (uint16_t)n, out, sizeof out);
    if (!len) return false;
    uart_write_blocking(UART, out, len);          // ~2 ms for a senses frame at 921600
    return true;
}

void link_poll(Body& body, void (*on_audio)(const uint8_t* pcm, uint16_t len)) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    while (g_tail != g_head) {
        uint8_t b = g_ring[g_tail];
        g_tail = (uint16_t)((g_tail + 1) % RING);
        if (!g_dec.push(b)) continue;
        const link::Frame& f = g_dec.frame();
        g_last_good_ms = now;
        g_have_frame = true;
        if (f.type == link::Type::Audio) {
            if (on_audio) on_audio(f.payload, f.len);
            else ++g_audio_dropped;
            continue;
        }
        char cmd[16];
        if (!json::get_string((const char*)f.payload, f.len, "cmd", cmd, sizeof cmd)) continue;
        char reply[128];
        apply_command(cmd, (const char*)f.payload, f.len, body, reply, sizeof reply);
        // Only a ping is answered on the wire. Everything else is fire and forget; a caller
        // that wants a status code uses HTTP.
        if (std::strcmp(cmd, "ping") == 0) link_send_json(reply);
    }
}

bool link_alive(uint32_t now_ms) { return g_have_frame && (now_ms - g_last_good_ms) < 2000; }
void link_count_audio_drop() { ++g_audio_dropped; }
LinkStats link_stats() { return LinkStats{g_dec.frames(), g_dec.bad(), g_audio_dropped}; }
}
