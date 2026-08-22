#pragma once
#include "hardware/gpio.h"
#include "pins.hpp"
namespace pip::drv {
inline void button_init() { gpio_init(pins::BUTTON); gpio_set_dir(pins::BUTTON, false); gpio_pull_up(pins::BUTTON); }
inline bool button_pressed() { return !gpio_get(pins::BUTTON); }   // wired to GND, active low
}
