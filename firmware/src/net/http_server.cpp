#include "net/http_server.hpp"
#include <cstdio>
#include <cstring>
#include "lwip/tcp.h"
#include "lwip/pbuf.h"
#include "pip/http.hpp"
namespace pip::net {
namespace {
constexpr int kConns = 4;
struct Conn { tcp_pcb* pcb = nullptr; char buf[1536]; size_t len = 0; bool used = false; uint8_t idle = 0; };
Conn g_conns[kConns];
Body* g_body = nullptr;
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
err_t on_poll(void* arg, tcp_pcb*) {
    Conn* c = static_cast<Conn*>(arg);
    if (!c) return ERR_OK;
    if (++c->idle < 10) return ERR_OK;          // tcp_poll interval 2 = ~1 s; reap after ~10 s idle
    tcp_abort(c->pcb); c->pcb = nullptr; conn_close(c);
    return ERR_ABRT;                            // mandatory after tcp_abort
}
err_t on_recv(void* arg, tcp_pcb* pcb, pbuf* p, err_t) {
    Conn* c = static_cast<Conn*>(arg);
    if (!p) { return conn_close(c) ? ERR_ABRT : ERR_OK; }
    size_t room = sizeof c->buf - 1 - c->len;
    size_t take = p->tot_len < room ? p->tot_len : room;
    pbuf_copy_partial(p, c->buf + c->len, (u16_t)take, 0);
    c->len += take;
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    c->idle = 0;
    http::Request req{};
    http::Parse st = http::parse_request(c->buf, c->len, req);
    if (st == http::Parse::Incomplete && take == room) st = http::Parse::Bad;   // buffer full, give up
    if (st == http::Parse::Incomplete) return ERR_OK;
    static char resp[512];
    size_t n = (st == http::Parse::Bad) ? http::build_response(resp, sizeof resp, 400, "{\"error\":\"bad request\"}", nullptr)
                                        : handle_request(req, *g_body, resp, sizeof resp);
    if (n == 0) n = http::build_response(resp, sizeof resp, 400, "{\"error\":\"response too large\"}", nullptr);
    if (tcp_write(pcb, resp, (u16_t)n, TCP_WRITE_FLAG_COPY) == ERR_OK) tcp_output(pcb);
    else ++g_write_fail_count;
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
    printf("pip: http server on :%u\n", port);
    return true;
}
}
