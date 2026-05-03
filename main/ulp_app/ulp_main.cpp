#include "ulp_lp_core.h"
#include "ulp_lp_core_gpio.h"
#include "ulp_lp_core_i2c.h"
#include "ulp_lp_core_utils.h"
#include "esp_log.h"
#include "ina226.hpp"
#include "ulp_state.h"
#include "ulp_Interp.hpp"
#include "../../components/app/current_calibration/include/CurrentCalib.h"

//校准系数，实测来的
constexpr int current_scale = 1114;
constexpr int voltage_scale = 1250;

constexpr uint32_t LP_CPU_FREQ_HZ = 20000000;
constexpr uint32_t current_dead_zone_uv = 3000;

#define MS_TO_US(ms) ((ms) * 1000)

#define LP_VAR __attribute__((section(".rtc.bss")))
/* 定义共享变量，存放在RTC内存中（.rtc.bss段） */
volatile uint32_t ulp_state LP_VAR;
volatile uint32_t log_data LP_VAR;
volatile uint32_t core_run_freq_hz LP_VAR;
volatile uint32_t voltage_uv LP_VAR;
volatile uint16_t voltage_register_raw LP_VAR;
volatile int32_t  current_uA LP_VAR;
volatile int16_t  shunt_register_raw LP_VAR;
volatile int32_t  Board_temperature LP_VAR; //单位0.01℃
volatile int32_t  meter_uah LP_VAR; //单位uAh
volatile int32_t  meter_uwh LP_VAR; //单位uWh
volatile CurrentCalib::params_t current_calib_params LP_VAR;

volatile uint32_t now_time_ms = 0;
UlpNonEquidistantInterp<int16_t,int16_t,6> current_interp;
ULP_CORE_STATE& ulp_state_p = *(ULP_CORE_STATE*)&(ulp_state);

static void lp_log(uint32_t log){
    log_data = log;
    ulp_state_p.ulp_state_bits.ulp_have_log = true;
}

uint32_t last_ina226_run_ms = 0;
uint16_t mask_enable = 0;
void ina226_run(){
    INA226::read_register(INA226::Register_enum::INA226_MASK_ENABLE,&mask_enable);
    if(!(mask_enable & (1 << 3))){ //CNVR位为0，说明没有转换完成
        return;
    }
    /* 读取电压寄存器 */
    INA226::read_register(INA226::Register_enum::INA226_BUS_VOLTAGE, (uint16_t*)&voltage_register_raw);
    voltage_uv = voltage_register_raw*voltage_scale;

    /* 读取电流寄存器 */
    INA226::read_register(INA226::Register_enum::INA226_SHUNT_VOLTAGE, (uint16_t*)&shunt_register_raw);
    if(std::abs((int32_t)shunt_register_raw * current_calib_params.current_base_K) < current_dead_zone_uv){ 
        current_uA = 0;
    } else {
        //插值补偿映射
        int32_t no_temp_cali_current_uA = current_calib_params.current_base_K * (int16_t)shunt_register_raw + current_interp.interpolate(shunt_register_raw) * 100;
        
        //温漂补偿
        int32_t delta_temp = (Board_temperature - CurrentCalib::BASE_TEMPERATURE) / 100;
        int32_t temp_comp_uA = (no_temp_cali_current_uA / 1000) * current_calib_params.temperature_K * delta_temp / 1000;
        
        //最终电流
        current_uA = no_temp_cali_current_uA - temp_comp_uA;
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
 * @brief : 加载电流校准参数
 * @return  {*}
 */
void load_current_calib_params(){
    for(int i = 0;i<sizeof(current_calib_params.points)/sizeof(current_calib_params.points[0]);i++){
        current_interp.set_point(i,current_calib_params.points[i].register_value,current_calib_params.points[i].offset_current_100uA);
    }
    current_interp.finish_load();
}
    
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

void check_reload_current_calib_params(){
    if(ulp_state_p.ulp_state_bits.ulp_reload_calib_params){
        load_current_calib_params();
        ulp_state_p.ulp_state_bits.ulp_reload_calib_params = false;
    }
}


/**
 * @brief : 更新电能值
 * @return  {*}
 */ 
static int64_t charge_accum_uAms = 0;   // 电荷累加器，单位 uA·ms
static int64_t energy_accum_uvAms = 0;  // 能量乘积累加器，单位 uA·uV·ms
void update_meter() {
    static uint32_t last_run_ms = 0;
    uint32_t delta_ms = now_time_ms - last_run_ms;   // 不处理回绕，直接减
    if (delta_ms == 0) return;

    // ----- 电量积分 (uAh) -----
    charge_accum_uAms += (int64_t)current_uA * delta_ms;
    const int64_t UAH_THRESHOLD = 3600000LL;   // 1 uAh = 3,600,000 uA·ms
    if (charge_accum_uAms >= UAH_THRESHOLD) {
        int64_t whole = charge_accum_uAms / UAH_THRESHOLD;
        meter_uah += (int32_t)whole;           // 更新 32 位电量变量
        charge_accum_uAms %= UAH_THRESHOLD;    // 保留余数
    } else if (charge_accum_uAms <= -UAH_THRESHOLD) {
        int64_t whole = (-charge_accum_uAms) / UAH_THRESHOLD;
        meter_uah -= (int32_t)whole;
        charge_accum_uAms = -((-charge_accum_uAms) % UAH_THRESHOLD);
    }

    // ----- 能量积分 (uWh) -----
    // 1 uWh = 3.6e12 uA·uV·ms
    energy_accum_uvAms += (int64_t)current_uA * voltage_uv * delta_ms;
    const int64_t UWH_THRESHOLD = 3600000000000LL;   // 3.6e12
    if (energy_accum_uvAms >= UWH_THRESHOLD) {
        int64_t whole = energy_accum_uvAms / UWH_THRESHOLD;
        meter_uwh += (int32_t)whole;
        energy_accum_uvAms %= UWH_THRESHOLD;
    } else if (energy_accum_uvAms <= -UWH_THRESHOLD) {
        int64_t whole = (-energy_accum_uvAms) / UWH_THRESHOLD;
        meter_uwh -= (int32_t)whole;
        energy_accum_uvAms = -((-energy_accum_uvAms) % UWH_THRESHOLD);
    }

    last_run_ms = now_time_ms;
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

uint32_t loop_times = 0;
uint32_t last_loop_times = 0;

int main(void){
    load_current_calib_params();
    ulp_ina226_init();
    ulp_state_p.ulp_state_bits.ulp_run = true;
    while (1) {
        ina226_run();
        timer_run();

        //计算循环运行频率
        app_loop_every_ms(1000, [](){
            core_run_freq_hz = loop_times - last_loop_times;
            last_loop_times = loop_times;
            //lp_log(core_run_freq_hz);
        });

        //检查是否需要重新加载校准参数
        app_loop_every_ms(20,check_reload_current_calib_params);

        // 电量积分
        app_loop_every_ms(10,update_meter);
            
        ++loop_times;
    }
}