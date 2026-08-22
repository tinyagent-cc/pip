#include "net/wifi.hpp"
#include <cstdio>
#include <cstring>
#include "pico/cyw43_arch.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
namespace pip::net {
namespace {
constexpr uint32_t kPollMs = 1000;      // how often the link status is read
constexpr uint32_t kRetryMs = 10000;    // wait after a failure before connecting again
constexpr uint32_t kJoinMs = 15000;     // give up on a join that resolves neither way

const char* g_ssid = nullptr;
const char* g_pass = nullptr;
WifiState g_state = WifiState::Idle;
uint32_t g_next_poll_ms = 0, g_retry_at_ms = 0, g_join_deadline_ms = 0;
char g_ip[16] = "0.0.0.0";

void start_connect(uint32_t now_ms) {
    cyw43_arch_lwip_begin();
    int rc = cyw43_arch_wifi_connect_async(g_ssid, g_pass, CYW43_AUTH_WPA2_AES_PSK);
    cyw43_arch_lwip_end();
    if (rc != 0) {
        printf("pip: wifi connect call failed rc=%d\n", rc);
        g_state = WifiState::Down;
        g_retry_at_ms = now_ms + kRetryMs;
        return;
    }
    g_state = WifiState::Connecting;
    g_join_deadline_ms = now_ms + kJoinMs;
}
void go_down(uint32_t now_ms, int status) {
    printf("pip: wifi down status=%d\n", status);
    std::strcpy(g_ip, "0.0.0.0");
    g_state = WifiState::Down;
    g_retry_at_ms = now_ms + kRetryMs;
}
}
void wifi_start_async(const char* ssid, const char* pass, uint32_t now_ms) {
    g_ssid = ssid; g_pass = pass;
    cyw43_arch_lwip_begin();
    cyw43_arch_enable_sta_mode();
    cyw43_arch_lwip_end();
    start_connect(now_ms);
    g_next_poll_ms = now_ms + kPollMs;
}
WifiState wifi_poll(uint32_t now_ms) {
    if (g_state == WifiState::Idle) return g_state;
    if ((int32_t)(now_ms - g_next_poll_ms) < 0) return g_state;
    g_next_poll_ms = now_ms + kPollMs;
    // Read the link and the address under the lock, log outside it: a USB CDC write is
    // slow enough to matter and has no business holding lwIP.
    char ip[16] = "0.0.0.0";
    cyw43_arch_lwip_begin();
    int status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    if (status == CYW43_LINK_UP)
        ip4addr_ntoa_r(netif_ip4_addr(&cyw43_state.netif[CYW43_ITF_STA]), ip, sizeof ip);
    cyw43_arch_lwip_end();

    if (status == CYW43_LINK_UP) {
        if (g_state != WifiState::Up) {
            std::memcpy(g_ip, ip, sizeof g_ip);
            g_state = WifiState::Up;
            printf("pip: wifi up ip=%s\n", g_ip);
        }
        return g_state;
    }
    if (g_state == WifiState::Up) { go_down(now_ms, status); return g_state; }
    if (g_state == WifiState::Connecting) {
        // A negative status is a verdict (FAIL, NONET, BADAUTH). DOWN, JOIN and NOIP all
        // mean "still trying", so those only count against the join deadline.
        if (status < 0 || (int32_t)(now_ms - g_join_deadline_ms) >= 0) go_down(now_ms, status);
        return g_state;
    }
    if ((int32_t)(now_ms - g_retry_at_ms) >= 0) start_connect(now_ms);
    return g_state;
}
const char* wifi_ip() { return g_ip; }
}
