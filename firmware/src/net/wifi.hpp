#pragma once
#include <cstdint>
namespace pip::net {
// Station mode, WPA2. Writes the dotted IP into ip_out on success.
bool wifi_connect(const char* ssid, const char* pass, uint32_t timeout_ms, char ip_out[16]);
}
