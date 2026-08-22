#include <cstdio>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pins.hpp"
#include "pip/face.hpp"
#ifndef PIP_LITE
#include "drivers/ili9341.hpp"
#endif

static pip::Framebuffer g_fb;

int main() {
    stdio_init_all();
    if (cyw43_arch_init() != 0) { printf("pip: cyw43 init failed\n"); return 1; }
    pip::Face face;
#ifndef PIP_LITE
    pip::drv::Ili9341 lcd;
    lcd.init(spi0, 40 * 1000 * 1000);
#endif
    // Demo loop until the protocol lands: cycle expressions every 3 s so the bench has something to look at.
    const pip::Emotion cycle[] = {pip::Emotion::Idle, pip::Emotion::Happy, pip::Emotion::Thinking, pip::Emotion::Wink, pip::Emotion::Sleepy, pip::Emotion::Alert};
    unsigned ci = 0;
    absolute_time_t next = get_absolute_time(), next_cycle = make_timeout_time_ms(3000);
    uint32_t last_ms = to_ms_since_boot(get_absolute_time());
    while (true) {
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        uint32_t dt = now_ms - last_ms; last_ms = now_ms;
        if (absolute_time_diff_us(get_absolute_time(), next_cycle) <= 0) {
            ci = (ci + 1) % 6; face.set_emotion(cycle[ci]);
            printf("pip: express %s\n", pip::emotion_name(cycle[ci]));
            next_cycle = make_timeout_time_ms(3000);
        }
        pip::Rect dirty = face.tick(dt, g_fb);
#ifndef PIP_LITE
        if (!dirty.empty()) lcd.push(g_fb, dirty);
#else
        (void)dirty;
#endif
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, (now_ms / 500) % 2);
        next = delayed_by_ms(next, 33);
        sleep_until(next);
    }
}
