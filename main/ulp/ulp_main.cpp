#include "ulp_lp_core.h"
#include "ulp_lp_core_gpio.h"
#include "ulp_lp_core_i2c.h"
#include "ulp_lp_core_utils.h"
#include "esp_log.h"
#include "ina226.hpp"
#define MS_TO_US(ms) ((ms) * 1000)

#define LP_VAR __attribute__((section(".rtc.bss")))
/* 定义共享变量，存放在RTC内存中（.rtc.bss段） */
volatile uint32_t ulp_counter LP_VAR;
volatile int ulp_last_gpio_state LP_VAR;
volatile bool have_log LP_VAR;
volatile uint32_t log_data LP_VAR;
volatile uint32_t voltage LP_VAR;
volatile uint32_t current LP_VAR;


static void lp_log(uint32_t log){
    log_data = log;
    have_log = true;
}

int main(void){

    constexpr lp_io_num_t Alert_Pin = LP_IO_NUM_1;
    ulp_lp_core_gpio_init(Alert_Pin);
    ulp_lp_core_gpio_pullup_enable(Alert_Pin);
    ulp_lp_core_gpio_input_enable(Alert_Pin);

    ulp_lp_core_delay_us(MS_TO_US(10));

    INA226::reset();

    ulp_lp_core_delay_us(MS_TO_US(100));

    INA226::set_configuration(
        INA226::Avg_times_enum::INA226_64_samples,
        INA226::Timing_enum::INA226_1100_us,
        INA226::Timing_enum::INA226_1100_us,
        INA226::Mode_enum::INA226_SHUNT_AND_BUS_CONTINUOUS);

    ulp_lp_core_delay_us(MS_TO_US(50));

    INA226::MaskEnable_reg_t MaskEnable_reg;
    MaskEnable_reg.raw = 0;
    MaskEnable_reg.bits.LEN=1;
    MaskEnable_reg.bits.APOL=0;
    MaskEnable_reg.bits.CNVR=1;
    INA226::write_register(INA226::Register_enum::INA226_MASK_ENABLE,MaskEnable_reg.raw);
    ulp_lp_core_delay_us(MS_TO_US(50));

    uint16_t bus_voltage = 0;
    uint16_t shunt_voltage = 0;
    while (1) {
        while (ulp_lp_core_gpio_get_level(Alert_Pin) == 1){
            ulp_lp_core_delay_us(1);
        };
        INA226::read_register(INA226::Register_enum::INA226_BUS_VOLTAGE, &bus_voltage);
        INA226::read_register(INA226::Register_enum::INA226_SHUNT_VOLTAGE, &shunt_voltage);
        INA226::read_register(INA226::Register_enum::INA226_MASK_ENABLE,nullptr);
    }
    return 0;
}