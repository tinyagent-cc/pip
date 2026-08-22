#include <cstdio>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pins.hpp"

int main() {
    stdio_init_all();
    if (cyw43_arch_init() != 0) { printf("pip: cyw43 init failed\n"); return 1; }
    printf("pip body v0 skeleton, button pin %u\n", pip::pins::BUTTON);
    while (true) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1); sleep_ms(100);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0); sleep_ms(900);
    }
}
