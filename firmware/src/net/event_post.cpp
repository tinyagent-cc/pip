#include "net/event_post.hpp"
#include <cstdio>
#include <cstring>
#include "lwip/tcp.h"
#include "lwip/ip_addr.h"
namespace pip::net {
namespace {
struct Post { tcp_pcb* pcb = nullptr; char req[320]; size_t len = 0; bool busy = false; uint8_t idle = 0; };
Post g_post;
// Clears callbacks and closes (or aborts) the pcb, then resets the in-flight flag.
// Returns true if the pcb was aborted (caller must then return ERR_ABRT to lwIP).
bool finish() {
    bool aborted = false;
    if (g_post.pcb) {
        tcp_arg(g_post.pcb, nullptr); tcp_recv(g_post.pcb, nullptr); tcp_err(g_post.pcb, nullptr); tcp_poll(g_post.pcb, nullptr, 0);
        if (tcp_close(g_post.pcb) != ERR_OK) { tcp_abort(g_post.pcb); aborted = true; }
    }
    g_post.pcb = nullptr; g_post.busy = false; g_post.idle = 0;
    return aborted;
}
void on_err(void*, err_t e) { printf("pip: event post err %d\n", (int)e); g_post.pcb = nullptr; finish(); }
err_t on_recv(void*, tcp_pcb* pcb, pbuf* p, err_t) {
    if (!p) { return finish() ? ERR_ABRT : ERR_OK; }
    tcp_recved(pcb, p->tot_len); pbuf_free(p);   // the brain's reply is not interesting; close once it arrives
    return finish() ? ERR_ABRT : ERR_OK;
}
err_t on_connected(void*, tcp_pcb* pcb, err_t err) {
    if (err != ERR_OK) { return finish() ? ERR_ABRT : ERR_OK; }
    tcp_write(pcb, g_post.req, (u16_t)g_post.len, TCP_WRITE_FLAG_COPY);
    tcp_output(pcb);
    return ERR_OK;
}
// tcp_poll interval 2 = ~1 s; reap after ~10 s idle if the brain never answers (mirrors http_server.cpp).
err_t on_poll(void*, tcp_pcb*) {
    if (++g_post.idle < 10) return ERR_OK;
    if (g_post.pcb) tcp_abort(g_post.pcb);
    g_post.pcb = nullptr; g_post.busy = false; g_post.idle = 0;
    return ERR_ABRT;   // mandatory after tcp_abort
}
}
bool post_event(const char* host_ip, uint16_t port, const char* json) {
    if (g_post.busy) { printf("pip: event dropped, post in flight\n"); return false; }
    ip_addr_t addr;
    if (!ipaddr_aton(host_ip, &addr)) return false;
    int n = std::snprintf(g_post.req, sizeof g_post.req,
        "POST /event HTTP/1.0\r\nHost: %s\r\nContent-Type: application/json\r\nContent-Length: %u\r\nConnection: close\r\n\r\n%s",
        host_ip, (unsigned)std::strlen(json), json);
    if (n < 0 || (size_t)n >= sizeof g_post.req) return false;
    g_post.len = (size_t)n;
    g_post.pcb = tcp_new();
    if (!g_post.pcb) return false;
    g_post.busy = true; g_post.idle = 0;
    tcp_arg(g_post.pcb, &g_post); tcp_err(g_post.pcb, on_err); tcp_recv(g_post.pcb, on_recv); tcp_poll(g_post.pcb, on_poll, 2);
    if (tcp_connect(g_post.pcb, &addr, port, on_connected) != ERR_OK) { finish(); return false; }
    return true;
}
}
