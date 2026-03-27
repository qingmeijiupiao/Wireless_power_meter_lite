#ifndef INA226_HPP
#define INA226_HPP
#include <stdint.h>
#include "ulp_lp_core_i2c.h"

namespace INA226 {

constexpr uint8_t I2C_ADDR = 0x40;
constexpr int I2C_TIMEOUT = 10*20000; //cpu cycles

enum Register_enum : uint8_t {
    INA226_CONFIGURATION = 0x00,
    INA226_SHUNT_VOLTAGE = 0x01,
    INA226_BUS_VOLTAGE = 0x02,
    INA226_POWER = 0x03,
    INA226_CURRENT = 0x04,
    INA226_CALIBRATION = 0x05,
    INA226_MASK_ENABLE = 0x06,
    INA226_ALERT_LIMIT = 0x07,
    INA226_MANUFACTURER = 0xFE,
    INA226_DIE_ID = 0xFF,
};


//  for BVCT and SVCT conversion timing.
enum Timing_enum : uint8_t {
    INA226_140_us  = 0,
    INA226_204_us  = 1,
    INA226_332_us  = 2,
    INA226_588_us  = 3,
    INA226_1100_us = 4,
    INA226_2100_us = 5,
    INA226_4200_us = 6,
    INA226_8300_us = 7
};

enum Avg_times_enum : uint8_t {
    INA226_1_samples = 0,
    INA226_4_samples = 1,
    INA226_16_samples = 2,
    INA226_64_samples = 3,
    INA226_128_samples = 4,
    INA226_256_samples = 5,
    INA226_512_samples = 6,
    INA226_1024_samples = 7,
};

enum Mode_enum : uint8_t {
    INA226_POWER_DOWN0 = 0,
    INA226_SHUNT_SIGNAL_ONLY = 1,
    INA226_BUS_SIGNAL_ONLY = 2,
    INA226_SHUNT_AND_BUS_ONLY = 3,
    INA226_POWER_DOWN1 = 4,
    INA226_SHUNT_CONTINUOUS = 5,
    INA226_BUS_CONTINUOUS = 6,
    INA226_SHUNT_AND_BUS_CONTINUOUS = 7,
};

union MaskEnable_reg_t {
    uint16_t raw;  // 完整寄存器值

    struct {
        uint16_t LEN      : 1;  // Bit 0: Alert Latch Enable  Alert引脚电平锁存
        uint16_t APOL     : 1;  // Bit 1: Alert Polarity 转换完成后Alert引脚的电平
        uint16_t OVF      : 1;  // Bit 2: Math Overflow Flag (只读)
        uint16_t CVRF     : 1;  // Bit 3: Conversion Ready Flag (只读)
        uint16_t AFF      : 1;  // Bit 4: Alert Function Flag (只读)
        uint16_t reserved1: 5;  // Bits 5-9: 保留
        uint16_t CNVR     : 1;  // Bit 10: Conversion Ready Enable Alert引脚转换完成使能
        uint16_t POL      : 1;  // Bit 11: Power Over-Limit Enable
        uint16_t BUL      : 1;  // Bit 12: Bus Under-Voltage Enable
        uint16_t BOL      : 1;  // Bit 13: Bus Over-Voltage Enable
        uint16_t SUL      : 1;  // Bit 14: Shunt Under-Voltage Enable
        uint16_t SOL      : 1;  // Bit 15: Shunt Over-Voltage Enable
    } bits;
};

esp_err_t read_register(Register_enum reg, uint16_t *val){
    uint8_t reg_byte = reg;
    uint8_t data[2];
    esp_err_t ret = lp_core_i2c_master_write_read_device(LP_I2C_NUM_0, I2C_ADDR,
                                                         &reg_byte, 1, data, 2, I2C_TIMEOUT);
    if(ret != ESP_OK){
        return ret;
    }

    if(val!=nullptr){
        *val = (data[0] << 8) | data[1];
    }
    return ESP_OK;
}

esp_err_t write_register(Register_enum reg, uint16_t val){
    uint8_t data[3] = {static_cast<uint8_t>(reg), static_cast<uint8_t>(val >> 8), static_cast<uint8_t>(val)};
    uint8_t reg_byte;//v5.5.3版本的IDF API不允许读取0字节
    esp_err_t ret = lp_core_i2c_master_write_read_device(LP_I2C_NUM_0, I2C_ADDR,
                                                         data, 3, &reg_byte, 1, I2C_TIMEOUT);
    if(ret != ESP_OK){
        return ret;
    }
    return ESP_OK;
}

esp_err_t set_configuration(Avg_times_enum avg_times,Timing_enum shunt_timing,Timing_enum bus_timing,Mode_enum mode){
    uint16_t config = (static_cast<uint16_t>(avg_times) << 9) |
                      (static_cast<uint16_t>(shunt_timing) << 6) |
                      (static_cast<uint16_t>(bus_timing) << 3) |
                      static_cast<uint16_t>(mode);
    return write_register(INA226_CONFIGURATION, config);
}

void reset(){
    write_register(INA226_CONFIGURATION, static_cast<uint16_t>(1<<15));
}

}//namespace INA226
#endif

