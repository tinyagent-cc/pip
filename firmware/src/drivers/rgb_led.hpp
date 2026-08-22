#pragma once
#include <initializer_list>
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "pins.hpp"
namespace pip::drv {
class RgbLed {
public:
    void init(bool common_anode) {
        anode_ = common_anode;
        for (unsigned p : {pins::LED_R, pins::LED_G, pins::LED_B}) {
            gpio_set_function(p, GPIO_FUNC_PWM);
            unsigned slice = pwm_gpio_to_slice_num(p);
            pwm_set_wrap(slice, 255);
            pwm_set_enabled(slice, true);
        }
        set(0, 0, 0);
    }
    void set(uint8_t r, uint8_t g, uint8_t b) {
        pwm_set_gpio_level(pins::LED_R, anode_ ? 255 - r : r);
        pwm_set_gpio_level(pins::LED_G, anode_ ? 255 - g : g);
        pwm_set_gpio_level(pins::LED_B, anode_ ? 255 - b : b);
    }
private:
    bool anode_ = false;
};
}
