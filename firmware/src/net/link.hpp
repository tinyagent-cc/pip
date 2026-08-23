#pragma once
#include <cstdint>
#include "pip/protocol.hpp"
namespace pip::net {
// The UART wire to the brain. The IRQ only fills a ring; every frame is decoded and acted
// on from the main loop, so a command never runs inside an interrupt.
void link_init(unsigned baud);                    // uart0 on pins::UART_TX / UART_RX, 8N1
// Drains the ring. Each JSON frame goes through apply_command with its "cmd"; a ping is
// answered with a {"pong":true} frame. AUDIO frames go to on_audio when it is non-null and
// are counted as dropped when it is not (Plan B gives them somewhere to go).
void link_poll(Body& body, void (*on_audio)(const uint8_t* pcm, uint16_t len));
bool link_send_json(const char* json);            // false when the JSON is over the 512-byte payload
bool link_alive(uint32_t now_ms);                 // a good frame arrived within the last 2 s
LinkStats link_stats();
}
