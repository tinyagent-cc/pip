#include "drivers/veml7700.hpp"
#include "hardware/gpio.h"
#include "pins.hpp"
namespace pip::drv {
namespace {
constexpr uint8_t ADDR = 0x10, REG_CONF = 0x00, REG_ALS = 0x04;
// A miswired or shorted bus must not hang boot: every transfer gives up after 20 ms.
constexpr uint32_t BUS_TIMEOUT_US = 20 * 1000;
}
bool Veml7700::init(i2c_inst_t* i2c) {
    i2c_ = i2c;
    i2c_init(i2c_, 100 * 1000);
    gpio_set_function(pins::I2C_SDA, GPIO_FUNC_I2C); gpio_set_function(pins::I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(pins::I2C_SDA); gpio_pull_up(pins::I2C_SCL);
    uint8_t conf[3] = {REG_CONF, 0x00, 0x00};   // gain 1x, IT 100 ms, power on
    return i2c_write_timeout_us(i2c_, ADDR, conf, 3, false, BUS_TIMEOUT_US) == 3;
}
bool Veml7700::read_lux(float& lux) {
    uint8_t reg = REG_ALS, buf[2];
    if (i2c_write_timeout_us(i2c_, ADDR, &reg, 1, true, BUS_TIMEOUT_US) != 1) return false;
    if (i2c_read_timeout_us(i2c_, ADDR, buf, 2, false, BUS_TIMEOUT_US) != 2) return false;
    lux = (float)(buf[0] | (buf[1] << 8)) * 0.0576f;
    return true;
}
}
