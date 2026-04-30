#include "ulp_lp_core.h"
#include "ulp_lp_core_gpio.h"
#include "ulp_lp_core_i2c.h"
#include "ulp_lp_core_utils.h"
#include "esp_log.h"
#include "ina226.hpp"
#include "ulp_state.h"
#include "ulp_interp.hpp"
//校准系数，实测来的
constexpr int current_scale = 1114;
constexpr int voltage_scale = 1250;
//电流校准点 <寄存器值,电流值uA>
const UlpNonEquidistantInterp<int16_t,int32_t,7>::Point current_interp_points[] = {
    {0,0},
    {1000,1000*current_scale},
    {2000,2000*current_scale},
    {3000,3000*current_scale},
    {4000,4000*current_scale},
    {5000,5000*current_scale},
    {32767,32767*current_scale},
};
UlpNonEquidistantInterp<int16_t,int32_t,7> current_interp;

constexpr uint32_t LP_CPU_FREQ_HZ = 20000000;
constexpr uint32_t current_dead_zone_uv = 3000;

#define MS_TO_US(ms) ((ms) * 1000)

#define LP_VAR __attribute__((section(".rtc.bss")))
/* 定义共享变量，存放在RTC内存中（.rtc.bss段） */
volatile uint32_t ulp_state LP_VAR;
volatile uint32_t log_data LP_VAR;
volatile uint32_t voltage_uv LP_VAR;
volatile uint32_t current_uA LP_VAR;
volatile uint32_t now_time_ms = 0;

ULP_CORE_STATE& ulp_state_p = *(ULP_CORE_STATE*)&(ulp_state);

static void lp_log(uint32_t log){
    log_data = log;
    ulp_state_p.ulp_state_bits.ulp_have_log = true;
}

uint16_t bus_voltage = 0;
int16_t shunt_voltage = 0;
uint32_t last_ina226_run_ms = 0;
uint16_t mask_enable = 0;
void ina226_run(){
    INA226::read_register(INA226::Register_enum::INA226_MASK_ENABLE,&mask_enable);
    if(!(mask_enable & (1 << 3))){ //CNVR位为0，说明没有转换完成
        return;
    }
    INA226::read_register(INA226::Register_enum::INA226_BUS_VOLTAGE, &bus_voltage);
    INA226::read_register(INA226::Register_enum::INA226_SHUNT_VOLTAGE, reinterpret_cast<uint16_t*>(&shunt_voltage));
    voltage_uv = bus_voltage*voltage_scale;
    if(std::abs(shunt_voltage*current_scale)<current_dead_zone_uv){ //死区，避免无输出时的噪声
        current_uA = 0;
    }else{
        current_uA = current_interp.interpolate((int16_t)shunt_voltage);
    }

    last_ina226_run_ms = now_time_ms;
}

// INA226初始化
void ulp_ina226_init(){
    INA226::reset();

    ulp_lp_core_delay_us(MS_TO_US(5));

    uint16_t temp_value = 0;
    INA226::read_register(INA226::Register_enum::INA226_MANUFACTURER,&temp_value);
    if(temp_value == 0){
        while(1){
            lp_log(0x11111111);
            ulp_lp_core_delay_us(MS_TO_US(3000));
        };
    }

    INA226::set_configuration(
        INA226::Avg_times_enum::INA226_64_samples,
        INA226::Timing_enum::INA226_1100_us,
        INA226::Timing_enum::INA226_1100_us,
        INA226::Mode_enum::INA226_SHUNT_AND_BUS_CONTINUOUS
    );

    INA226::MaskEnable_reg_t MaskEnable_reg;
    MaskEnable_reg.raw = 0;
    MaskEnable_reg.bits.LEN=1;
    MaskEnable_reg.bits.APOL=0;
    MaskEnable_reg.bits.CNVR=1;
    INA226::write_register(INA226::Register_enum::INA226_MASK_ENABLE,MaskEnable_reg.raw);
    while(voltage_uv == 0){
        ina226_run();
    }
    ulp_state_p.ulp_state_bits.ulp_ina226_init_ok = true;
};

    
/**
 * @brief : 定时器运行函数,更新ulp_core的内部时间计数器,单位为ms
 * @note  : 需要这个函数是因为ulp_lp_core_get_cpu_cycles返回的是cpu周期数,大约215s就会溢出，必须处理溢出情况
 * @return  {*}
 */
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

/**
 * @brief : APP 循环函数,每interval_ms执行一次action
 * @return  {*}
 * @param {uint32_t} interval_ms 运行的间隔时间，单位为ms
 * @param {F&&} action 循执行的函数可以是lambda表达式,也可以是普通函数
 */
template<typename F>
void app_loop_every_ms(uint32_t interval_ms, F&& action) {
    static uint32_t last_run_ms = 0;
    if ((now_time_ms - last_run_ms) > interval_ms) {
        last_run_ms = now_time_ms;
        action();
    }
}

int main(void){
    current_interp.load(current_interp_points);
    ulp_ina226_init();
    ulp_state_p.ulp_state_bits.ulp_run = true;
    while (1) {
        ina226_run();
        timer_run();
        app_loop_every_ms(5000, [](){
            // lp_log(now_time_ms);
        });
    }
}