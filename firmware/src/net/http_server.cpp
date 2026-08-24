#include "net/http_server.hpp"
#include "lwip/tcp.h"
#include "lwip/pbuf.h"
#include "pip/http.hpp"
namespace pip::net {
namespace {
constexpr int kConns = 4;
struct Conn { tcp_pcb* pcb = nullptr; char buf[1536]; size_t len = 0; bool used = false; uint8_t idle = 0; };
Conn g_conns[kConns];
Body* g_body = nullptr;
char g_chunk[1536];                // one segment staged out of its pbuf for http::feed
char g_resp[512];                  // one response at a time: callbacks never overlap
unsigned g_write_fail_count = 0;   // bumped on tcp_write failure; no printf in a callback

// Returns true if the pcb was aborted (caller must then return ERR_ABRT to lwIP).
bool conn_close(Conn* c) {
    bool aborted = false;
    if (c->pcb) {
        tcp_arg(c->pcb, nullptr); tcp_recv(c->pcb, nullptr); tcp_err(c->pcb, nullptr);
        tcp_sent(c->pcb, nullptr); tcp_poll(c->pcb, nullptr, 0);
        if (tcp_close(c->pcb) != ERR_OK) { tcp_abort(c->pcb); aborted = true; }
    }
    c->pcb = nullptr; c->len = 0; c->used = false;
    return aborted;
}
void on_err(void* arg, err_t) { Conn* c = static_cast<Conn*>(arg); if (c) { c->pcb = nullptr; (void)conn_close(c); } }
// Reap an idle connection. Same order as conn_close: clear the callbacks first, so the
// tcp_abort below cannot re-enter on_err from inside this call.
err_t on_poll(void* arg, tcp_pcb*) {
    Conn* c = static_cast<Conn*>(arg);
    if (!c) return ERR_OK;
    if (++c->idle < 10) return ERR_OK;          // tcp_poll interval 2 = ~1 s; reap after ~10 s idle
    if (c->pcb) {
        tcp_arg(c->pcb, nullptr); tcp_recv(c->pcb, nullptr); tcp_err(c->pcb, nullptr);
        tcp_sent(c->pcb, nullptr); tcp_poll(c->pcb, nullptr, 0);
        tcp_abort(c->pcb);
    }
    c->pcb = nullptr; c->len = 0; c->used = false; c->idle = 0;
    return ERR_ABRT;                            // mandatory after tcp_abort
}
err_t on_recv(void* arg, tcp_pcb* pcb, pbuf* p, err_t) {
    Conn* c = static_cast<Conn*>(arg);
    if (!c) { if (p) { tcp_recved(pcb, p->tot_len); pbuf_free(p); } return ERR_OK; }
    if (!p) { return conn_close(c) ? ERR_ABRT : ERR_OK; }
    // Stage the segment, then let core decide. A segment past the staging buffer is
    // truncated here, which http::feed sees as a full buffer and answers 400: the same
    // outcome the connection buffer would have produced on its own.
    size_t n = p->tot_len < sizeof g_chunk ? p->tot_len : sizeof g_chunk;
    pbuf_copy_partial(p, g_chunk, (u16_t)n, 0);
    tcp_recved(pcb, p->tot_len);   // the whole segment, not the copied part: keeps lwIP on the FIN path
    pbuf_free(p);
    c->idle = 0;
    size_t rlen = 0;
    http::Feed f = http::feed(c->buf, sizeof c->buf, c->len, g_chunk, n, *g_body, g_resp, sizeof g_resp, rlen);
    if (f == http::Feed::NeedMore) return ERR_OK;
    if (rlen) {
        if (tcp_write(pcb, g_resp, (u16_t)rlen, TCP_WRITE_FLAG_COPY) == ERR_OK) tcp_output(pcb);
        else ++g_write_fail_count;
    }
    return conn_close(c) ? ERR_ABRT : ERR_OK;   // close after write: lwIP flushes queued data before FIN
}
err_t on_accept(void*, tcp_pcb* newpcb, err_t err) {
    if (err != ERR_OK || !newpcb) { if (newpcb) { tcp_abort(newpcb); return ERR_ABRT; } return ERR_VAL; }
    Conn* c = nullptr;
    for (Conn& k : g_conns) if (!k.used) { c = &k; break; }
    if (!c) { tcp_abort(newpcb); return ERR_ABRT; }
    c->used = true; c->pcb = newpcb; c->len = 0; c->idle = 0;
    tcp_arg(newpcb, c); tcp_recv(newpcb, on_recv); tcp_err(newpcb, on_err); tcp_poll(newpcb, on_poll, 2);
    return ERR_OK;
}
}
bool http_server_start(uint16_t port, Body& body) {
    g_body = &body;
    tcp_pcb* pcb = tcp_new();
    if (!pcb) return false;
    if (tcp_bind(pcb, IP_ANY_TYPE, port) != ERR_OK) { tcp_close(pcb); return false; }
    tcp_pcb* lpcb = tcp_listen_with_backlog(pcb, 4);
    if (!lpcb) { tcp_close(pcb); return false; }
    tcp_accept(lpcb, on_accept);
    return true;   // the caller holds the lwIP lock, so it does the logging
}
void http_server_reset() {
    for (Conn& k : g_conns) { k.used = false; k.pcb = nullptr; k.len = 0; k.idle = 0; }
}
}
