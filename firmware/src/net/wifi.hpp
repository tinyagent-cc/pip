#pragma once
#include <cstdint>
namespace pip::net {
enum class WifiState : uint8_t { Idle, Connecting, Up, Down };
// Station mode, WPA2, non-blocking throughout: nothing here waits on the radio, so the
// face keeps animating whether the AP answers, refuses, or disappears mid-run.
// ssid and pass must outlive the call (the config.h literals do).
void wifi_start_async(const char* ssid, const char* pass, uint32_t now_ms);
// Call every frame. Checks the link about once a second, logs each state change, and
// reissues the connect about 10 s after a failure or a drop. Returns the current state.
WifiState wifi_poll(uint32_t now_ms);
// Dotted IPv4 of the station interface, "0.0.0.0" whenever the link is not up.
const char* wifi_ip();
}
