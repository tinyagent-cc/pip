#pragma once
#include <cstdint>
namespace pip::net {
// One-shot "POST /event" with a JSON body, no retry, at most one in flight. Caller holds the lwIP lock.
bool post_event(const char* host_ip, uint16_t port, const char* json);
}
