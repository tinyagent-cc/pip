#include "net/wifi.hpp"
#include <cstdio>
#include "pico/cyw43_arch.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
namespace pip::net {
bool wifi_connect(const char* ssid, const char* pass, uint32_t timeout_ms, char ip_out[16]) {
    cyw43_arch_enable_sta_mode();
    int rc = cyw43_arch_wifi_connect_timeout_ms(ssid, pass, CYW43_AUTH_WPA2_AES_PSK, timeout_ms);
    if (rc != 0) { printf("pip: wifi connect failed rc=%d\n", rc); return false; }
    ip4addr_ntoa_r(netif_ip4_addr(netif_default), ip_out, 16);
    return true;
}
}
