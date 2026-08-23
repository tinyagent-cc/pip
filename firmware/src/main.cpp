#include <cmath>
#include <cstdio>
#include <cstring>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pins.hpp"
#include "config.h"
#include "body.hpp"
#include "net/wifi.hpp"
#include "net/http_server.hpp"
#include "net/event_post.hpp"
#include "net/link.hpp"
#include "pip/face.hpp"
#include "pip/hud.hpp"
#include "pip/events.hpp"
#include "drivers/button.hpp"
#include "drivers/temp.hpp"
#ifndef PIP_LITE
#include "drivers/ili9341.hpp"
#include "drivers/rgb_led.hpp"
#include "drivers/veml7700.hpp"
#endif

static pip::Framebuffer g_fb;

int main() {
    stdio_init_all();
    if (cyw43_arch_init() != 0) { printf("pip: cyw43 init failed\n"); return 1; }
    pip::Face face;
    pip::Hud hud;
#ifndef PIP_LITE
    pip::drv::Ili9341 lcd;
    lcd.init(spi0, 40 * 1000 * 1000);
#endif
    pip::drv::button_init();
    pip::drv::temp_init();
    pip::ButtonFsm btn;
    pip::LightFsm light;
    float lux = -1.0f, temp_c = 0.0f;
#ifndef PIP_LITE
    pip::drv::RgbLed rgb; rgb.init(PIP_RGB_COMMON_ANODE);
    uint8_t led_r = 0, led_g = 0, led_b = 0;   // last colour /led asked for, restored after a button press
    pip::drv::Veml7700 als; bool have_als = als.init(i2c0);
    printf("pip: veml7700 %s\n", have_als ? "ok" : "absent");
#endif
    // Paint one frame before touching the radio. Otherwise the panel shows whatever was in
    // its RAM until the first loop iteration, which is the first thing anyone sees on boot.
    pip::Rect boot = face.tick(0, g_fb);
    pip::Rect boot_hud = hud.draw(g_fb, true);
#ifndef PIP_LITE
    lcd.push(g_fb, boot);
    lcd.push(g_fb, boot_hud);
#else
    (void)boot; (void)boot_hud;
#endif
    static pip::RealBody body;
    // The wire comes up before the radio: it needs no association and the brain may already
    // be talking. A hello is how the brain learns the body rebooted.
    pip::net::link_init(921600);
    pip::net::link_send_json("{\"hello\":{\"fw\":\"v1\",\"protocol\":1}}");
    bool online = false, server_started = false;
    uint32_t next_server_try_ms = 0;   // a failed start retries every 5 s, not every frame
    pip::net::wifi_start_async(PIP_WIFI_SSID, PIP_WIFI_PASS, to_ms_since_boot(get_absolute_time()));
    uint32_t next_sense_ms = 0, next_uplink_ms = 0;
    absolute_time_t next = get_absolute_time();
    uint32_t last_ms = to_ms_since_boot(get_absolute_time());
    while (true) {
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        uint32_t dt = now_ms - last_ms; last_ms = now_ms;
        pip::net::link_poll(body, nullptr);
        bool wire = pip::net::link_alive(now_ms);
        online = pip::net::wifi_poll(now_ms) == pip::net::WifiState::Up;
        if (online && !server_started && (int32_t)(now_ms - next_server_try_ms) >= 0) {
            next_server_try_ms = now_ms + 5000;
            cyw43_arch_lwip_begin();
            server_started = pip::net::http_server_start(PIP_HTTP_PORT, body);
            cyw43_arch_lwip_end();
            if (server_started) printf("pip: http server on :%u\n", (unsigned)PIP_HTTP_PORT);
            else printf("pip: http server failed to start on :%u\n", (unsigned)PIP_HTTP_PORT);
        }
        pip::Event ev = btn.tick(now_ms, pip::drv::button_pressed());
        if (now_ms >= next_sense_ms) {
            next_sense_ms = now_ms + 500;
            temp_c = pip::drv::temp_read_c();
#ifndef PIP_LITE
            if (have_als) {
                // A failed read is not a reading. NAN keeps the last value out of /senses
                // (protocol.cpp sanitizes it to -1) and the FSM ignores it outright.
                if (!als.read_lux(lux)) lux = NAN;
                pip::Event lev = light.tick(now_ms, lux);
                if (lev != pip::Event::None) ev = lev;   // at most one event per frame is fine at 30 fps
            }
#endif
        }
        body.publish(lux, temp_c, btn.down());
        body.publish_link(pip::net::link_stats(), pip::AudioStats{});
        pip::Emotion pe; if (body.take_emotion(pe)) { face.set_emotion(pe); printf("pip: express %s\n", pip::emotion_name(pe)); }
        pip::Chirp pc; if (body.take_chirp(pc)) printf("pip: chirp %s (audio lands with Plan 3b)\n", pip::chirp_name(pc));
        char text[96]; if (body.take_say(text, sizeof text)) { face.say(text, 3000); printf("pip: say %s\n", text); }
        pip::HudUpdate hu; if (body.take_hud(hu)) hud.apply(hu);
        char scene[16];
        if (body.take_scene(scene, sizeof scene)) {
            // A scene is a HUD caption on the body's side; the script behind the name runs
            // on the brain.
            pip::HudUpdate su; su.has_scene = true; std::snprintf(su.scene, sizeof su.scene, "%s", scene);
            hud.apply(su);
            printf("pip: scene %s\n", scene);
        }
        uint8_t r, g, b; if (body.take_led(r, g, b)) {
#ifndef PIP_LITE
            led_r = r; led_g = g; led_b = b;
            rgb.set(r, g, b);
#else
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, (r | g | b) != 0);
#endif
            printf("pip: led %u %u %u\n", r, g, b);
        }
        face.set_night(light.is_low());
        hud.set_senses(lux, light.is_low(), temp_c, wire, online);
        if (ev != pip::Event::None) {
            printf("pip: event %s (lux=%.1f temp=%.1f)\n", pip::event_name(ev), (double)lux, (double)temp_c);
#ifndef PIP_LITE
            // The button borrows the LED for the length of the touch, then hands it back.
            // /led is the brain's mood channel and a press must not silently destroy it.
            if (ev == pip::Event::ButtonPress || ev == pip::Event::ButtonHold) rgb.set(0, 40, 0);
            if (ev == pip::Event::ButtonRelease) rgb.set(led_r, led_g, led_b);
#endif
            char js[64]; pip::event_json(pip::event_name(ev), js, sizeof js);
            // The wire wins when it is alive: it is an order of magnitude faster than an
            // HTTP round trip, and the press-to-wink number in the demo is measured on it.
            if (wire) { if (!pip::net::link_send_json(js)) printf("pip: event too long for the wire\n"); }
            else if (online) {
                cyw43_arch_lwip_begin();
                bool queued = pip::net::post_event(PIP_BRAIN_HOST, PIP_BRAIN_PORT, js);
                cyw43_arch_lwip_end();
                if (!queued) printf("pip: event dropped\n");
            }
        }
        if (wire && now_ms >= next_uplink_ms) {
            next_uplink_ms = now_ms + 500;
            char sj[224], frame[256];
            if (pip::senses_json(body.senses(), sj, sizeof sj)) {
                std::snprintf(frame, sizeof frame, "{\"senses\":%s}", sj);
                pip::net::link_send_json(frame);
            }
        }
        pip::Rect dirty = face.tick(dt, g_fb);
        pip::Rect hdirty = hud.draw(g_fb, false);
#ifndef PIP_LITE
        if (!dirty.empty()) lcd.push(g_fb, dirty);
        if (!hdirty.empty()) lcd.push(g_fb, hdirty);
#else
        (void)dirty; (void)hdirty;
#endif
#ifndef PIP_LITE
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, (now_ms / 500) % 2);
#endif
        // Resync instead of accumulating: one slow frame (a full-screen push is ~31 ms of
        // SPI) would otherwise leave next permanently in the past and the loop free-running.
        next = delayed_by_ms(next, 33);
        if (absolute_time_diff_us(get_absolute_time(), next) < 0) next = get_absolute_time();
        sleep_until(next);
    }
}
