#pragma once
#include "hardware/i2c.h"
namespace pip::drv {
// VEML7700 ambient light sensor, gain 1x, 100 ms integration (0.0576 lux/count).
class Veml7700 {
public:
    bool init(i2c_inst_t* i2c);      // false when the sensor does not ACK
    bool read_lux(float& lux);
private:
    i2c_inst_t* i2c_ = nullptr;
};
}
