#include "ulp_lp_core.h"
#include "ulp_lp_core_gpio.h"
#include "ulp_lp_core_i2c.h"
#include "ulp_lp_core_utils.h"
#include "esp_log.h"
#include "ina226.hpp"
constexpr uint32_t LP_CPU_FREQ_HZ = 20000000;
constexpr lp_io_num_t Alert_Pin = LP_IO_NUM_1;

#define MS_TO_US(ms) ((ms) * 1000)

#define LP_VAR __attribute__((section(".rtc.bss")))
/* 定义共享变量，存放在RTC内存中（.rtc.bss段） */
volatile uint32_t ulp_counter LP_VAR;
volatile int ulp_last_gpio_state LP_VAR;
volatile bool have_log LP_VAR;
volatile uint32_t log_data LP_VAR;
volatile uint32_t voltage LP_VAR;
volatile uint32_t current LP_VAR;

uint32_t now_time_ms = 0;

static void lp_log(uint32_t log){
    log_data = log;
    have_log = true;
}


void ulp_gpio_init(){
    ulp_lp_core_gpio_init(Alert_Pin);
    ulp_lp_core_gpio_pullup_enable(Alert_Pin);
    ulp_lp_core_gpio_input_enable(Alert_Pin);
    ulp_lp_core_delay_us(MS_TO_US(10));
};


// I2C 初始化
void ulp_i2c_init(){
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
};

    
uint16_t bus_voltage = 0;
uint16_t shunt_voltage = 0;
void ina226_run(){
    if(ulp_lp_core_gpio_get_level(Alert_Pin) == 1){
        return;
    }
    INA226::read_register(INA226::Register_enum::INA226_BUS_VOLTAGE, &bus_voltage);
    INA226::read_register(INA226::Register_enum::INA226_SHUNT_VOLTAGE, &shunt_voltage);
    INA226::read_register(INA226::Register_enum::INA226_MASK_ENABLE,nullptr);
}

void timer_run(void) {
    constexpr uint32_t CYCLES_PER_MS = LP_CPU_FREQ_HZ / 1000;
    constexpr uint32_t MAX_MS = 0xFFFFFFFF / CYCLES_PER_MS;
    static uint32_t last_raw_ms = 0;

    int32_t current_ms = ulp_lp_core_get_cpu_cycles() / CYCLES_PER_MS;
    int32_t diff = current_ms - last_raw_ms;

    if (now_time_ms == 0) {
        now_time_ms = current_ms;
        last_raw_ms = current_ms;
        return;
    }

    if (diff < 0) {  // 发生回绕
        now_time_ms += (MAX_MS - last_raw_ms) + current_ms;
    } else {
        now_time_ms += diff;
    }
    last_raw_ms = current_ms;
}

void test_func(void){
    static uint32_t last_run_ms = 0;
    if(now_time_ms - last_run_ms > 1000){
        last_run_ms = now_time_ms;
        lp_log(now_time_ms);
    }
}

int main(void){
    ulp_gpio_init();
    ulp_i2c_init();

    while (1) {
        ina226_run();
        timer_run();
        test_func();
    }
}