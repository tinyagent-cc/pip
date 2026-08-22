#pragma once
#include "hardware/adc.h"
namespace pip::drv {
inline void temp_init() { adc_init(); adc_set_temp_sensor_enabled(true); }
inline float temp_read_c() {
    adc_select_input(4);
    float v = adc_read() * 3.3f / 4096.0f;
    return 27.0f - (v - 0.706f) / 0.001721f;   // RP2350 datasheet formula
}
}
