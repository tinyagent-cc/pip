#pragma once
#include <cstdint>
#include "pip/protocol.hpp"
namespace pip::net {
// Raw-tcp HTTP/1.0 server for PROTOCOL.md v0. One request per connection. Four concurrent
// connections; extra ones are closed. Call once from main after the link comes up, holding
// the lwIP lock. It logs nothing, because the caller holds that lock.
bool http_server_start(uint16_t port, Body& body);
}
