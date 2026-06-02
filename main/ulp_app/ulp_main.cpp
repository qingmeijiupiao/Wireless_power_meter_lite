#include "ulp_lp_core.h"
#include "ulp_lp_core_gpio.h"
#include "ulp_lp_core_i2c.h"
#include "ulp_lp_core_critical_section_shared.h"
#include "ulp_lp_core_utils.h"
#include "esp_log.h"
#include <cstddef>
#include "ina226.hpp"
#include "ulp_state.h"
#include "ulp_Interp.hpp"
#include "../../components/app/current_calibration/include/CurrentCalib.h"

//校准系数，实测来的
constexpr int current_scale = 1114;
constexpr int voltage_scale = 1250;

constexpr uint32_t LP_CPU_FREQ_HZ = 20000000;
constexpr uint32_t current_dead_zone_uv = 5000;

#define MS_TO_US(ms) ((ms) * 1000)

#define LP_VAR __attribute__((section(".rtc.bss")))
/* 定义共享变量，存放在RTC内存中（.rtc.bss段） */
volatile uint32_t ulp_state LP_VAR;
ulp_lp_core_spinlock_t shared_lock LP_VAR;
volatile uint32_t log_data LP_VAR;
volatile uint32_t core_run_freq_hz LP_VAR;
volatile uint32_t voltage_uv LP_VAR;
volatile uint16_t voltage_register_raw LP_VAR;
volatile int32_t  current_uA LP_VAR;
volatile int16_t  shunt_register_raw LP_VAR;
volatile uint16_t ina226_manufacturer_id LP_VAR;
volatile int32_t  Board_temperature LP_VAR; //单位0.01℃
volatile int64_t  meter_uah LP_VAR; //单位uAh
volatile int64_t  meter_uwh LP_VAR; //单位uWh
volatile CurrentCalib::params_t current_calib_params LP_VAR;

volatile uint32_t now_time_ms = 0;
UlpNonEquidistantInterp<int16_t,int16_t,6> current_interp;
ULP_CORE_STATE& ulp_state_p = *(ULP_CORE_STATE*)&(ulp_state);
// 计算过程使用 LP 本地副本，避免每次采样长时间占用跨核锁。
CurrentCalib::params_t local_current_calib_params;
uint32_t last_ina226_run_ms = 0;

struct MeterSample {
    int32_t current_uA;
    uint32_t voltage_uv;
};

template<typename F>
auto with_shared_lock(F&& action) {
    // LP 内存有限，不使用会引入析构清理代码的 RAII；临界区只包装短小的 RTC 访问。
    ulp_lp_core_enter_critical(&shared_lock);
    auto result = action();
    ulp_lp_core_exit_critical(&shared_lock);
    return result;
}

template<typename F>
void with_shared_lock_void(F&& action) {
    ulp_lp_core_enter_critical(&shared_lock);
    action();
    ulp_lp_core_exit_critical(&shared_lock);
}

static MeterSample read_meter_sample() {
    // 电流和电压必须来自同一个采样快照，避免积分时组合到不同批次的数据。
    return with_shared_lock([]() {
        return MeterSample {
            .current_uA = current_uA,
            .voltage_uv = voltage_uv,
        };
    });
}

static void publish_sample(uint16_t new_voltage_register_raw,
                           int16_t new_shunt_register_raw,
                           uint32_t new_voltage_uv,
                           int32_t new_current_uA) {
    // I2C 读取和补偿计算已在锁外完成，这里只原子提交一组共享采样值。
    with_shared_lock_void([=]() {
        voltage_register_raw = new_voltage_register_raw;
        shunt_register_raw = new_shunt_register_raw;
        voltage_uv = new_voltage_uv;
        current_uA = new_current_uA;
        last_ina226_run_ms = now_time_ms;
        ulp_state_p.ulp_state_bits.ulp_ina226_read_timeout = false;
    });
}

static void add_meter_totals(int64_t charge_uah, int64_t energy_uwh) {
    if (charge_uah == 0 && energy_uwh == 0) {
        return;
    }
    // 两个 64 位累计值需要在同一临界区提交，供 HP 核读取一致快照。
    with_shared_lock_void([=]() {
        meter_uah += charge_uah;
        meter_uwh += energy_uwh;
    });
}

static CurrentCalib::params_t read_current_calib_params() {
    // RTC 参数带 volatile，逐字段复制后再在锁外重建插值表。
    return with_shared_lock([]() {
        CurrentCalib::params_t params = {};
        params.current_base_K = current_calib_params.current_base_K;
        for (size_t i = 0; i < sizeof(params.points) / sizeof(params.points[0]); ++i) {
            params.points[i].register_value = current_calib_params.points[i].register_value;
            params.points[i].offset_current_100uA = current_calib_params.points[i].offset_current_100uA;
        }
        params.temperature_K = current_calib_params.temperature_K;
        return params;
    });
}

static void lp_log(uint32_t log){
    with_shared_lock_void([=]() {
        log_data = log;
        ulp_state_p.ulp_state_bits.ulp_have_log = true;
    });
}

uint16_t mask_enable = 0;
constexpr uint32_t INA226_READ_TIMEOUT_MS = 1000;

void check_ina226_timeout(){
    if ((now_time_ms - last_ina226_run_ms) <= INA226_READ_TIMEOUT_MS) {
        return;
    }

    // 采样失效后不再暴露陈旧值。电压清零会让 HP 核的 UVP 链路关闭输出。
    with_shared_lock_void([]() {
        voltage_uv = 0;
        current_uA = 0;
        ulp_state_p.ulp_state_bits.ulp_ina226_read_timeout = true;
    });
}

void ina226_run(){
    if (INA226::read_register(INA226::Register_enum::INA226_MASK_ENABLE, &mask_enable) != ESP_OK) {
        check_ina226_timeout();
        return;
    }
    if(!(mask_enable & (1 << 3))){ //CNVR位为0，说明没有转换完成
        check_ina226_timeout();
        return;
    }

    uint16_t new_voltage_register_raw = 0;
    int16_t new_shunt_register_raw = 0;
    /* 读取电压寄存器 */
    if (INA226::read_register(INA226::Register_enum::INA226_BUS_VOLTAGE, &new_voltage_register_raw) != ESP_OK) {
        check_ina226_timeout();
        return;
    }

    /* 读取电流寄存器 */
    if (INA226::read_register(INA226::Register_enum::INA226_SHUNT_VOLTAGE,
                              (uint16_t*)&new_shunt_register_raw) != ESP_OK) {
        check_ina226_timeout();
        return;
    }

    const uint32_t new_voltage_uv = new_voltage_register_raw * voltage_scale;
    int32_t new_current_uA = 0;
    const int32_t board_temperature = with_shared_lock([]() {
        return Board_temperature;
    });
    if(std::abs((int32_t)new_shunt_register_raw * local_current_calib_params.current_base_K) < current_dead_zone_uv){
        new_current_uA = 0;
    } else {
        //插值补偿映射
        int32_t no_temp_cali_current_uA = local_current_calib_params.current_base_K * new_shunt_register_raw + current_interp.interpolate(new_shunt_register_raw) * 100;
        
        //温漂补偿
        int32_t delta_temp = (board_temperature - CurrentCalib::BASE_TEMPERATURE) / 100;
        int32_t temp_comp_uA = (no_temp_cali_current_uA / 1000) * local_current_calib_params.temperature_K * delta_temp / 1000;
        
        //最终电流
        new_current_uA = no_temp_cali_current_uA - temp_comp_uA;
    }

    publish_sample(new_voltage_register_raw, new_shunt_register_raw, new_voltage_uv, new_current_uA);
}

// INA226初始化
bool ulp_ina226_init(){
    if (INA226::reset() != ESP_OK) {
        with_shared_lock_void([]() {
            ulp_state_p.ulp_state_bits.ulp_i2c_init_err = true;
        });
        return false;
    }

    ulp_lp_core_delay_us(MS_TO_US(5));

    uint16_t new_ina226_manufacturer_id = 0;
    if (INA226::read_register(INA226::Register_enum::INA226_MANUFACTURER,
                              &new_ina226_manufacturer_id) != ESP_OK) {
        with_shared_lock_void([]() {
            ulp_state_p.ulp_state_bits.ulp_i2c_init_err = true;
        });
        return false;
    }
    with_shared_lock_void([=]() {
        ina226_manufacturer_id = new_ina226_manufacturer_id;
    });
    if (INA226::set_configuration(
        INA226::Avg_times_enum::INA226_64_samples,
        INA226::Timing_enum::INA226_1100_us,
        INA226::Timing_enum::INA226_1100_us,
        INA226::Mode_enum::INA226_SHUNT_AND_BUS_CONTINUOUS
    ) != ESP_OK) {
        with_shared_lock_void([]() {
            ulp_state_p.ulp_state_bits.ulp_i2c_init_err = true;
        });
        return false;
    }

    INA226::MaskEnable_reg_t MaskEnable_reg;
    MaskEnable_reg.raw = 0;
    MaskEnable_reg.bits.LEN=1;
    MaskEnable_reg.bits.APOL=0;
    MaskEnable_reg.bits.CNVR=1;
    if (INA226::write_register(INA226::Register_enum::INA226_MASK_ENABLE,MaskEnable_reg.raw) != ESP_OK) {
        with_shared_lock_void([]() {
            ulp_state_p.ulp_state_bits.ulp_i2c_init_err = true;
        });
        return false;
    }
    while(true){
        ina226_run();
        if (with_shared_lock([]() {
            return voltage_uv != 0;
        })) {
            break;
        }
        ulp_lp_core_delay_us(100);
    }
    with_shared_lock_void([]() {
        ulp_state_p.ulp_state_bits.ulp_ina226_init_ok = true;
    });
    return true;
};

/**
 * @brief : 加载电流校准参数
 * @return  {*}
 */
void load_current_calib_params(){
    local_current_calib_params = read_current_calib_params();
    for(int i = 0;i<sizeof(local_current_calib_params.points)/sizeof(local_current_calib_params.points[0]);i++){
        current_interp.set_point(i,local_current_calib_params.points[i].register_value,local_current_calib_params.points[i].offset_current_100uA);
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
    const bool reload = with_shared_lock([]() {
        const bool requested = ulp_state_p.ulp_state_bits.ulp_reload_calib_params;
        ulp_state_p.ulp_state_bits.ulp_reload_calib_params = false;
        return requested;
    });
    if(reload){
        load_current_calib_params();
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
    const MeterSample sample = read_meter_sample();

    charge_accum_uAms += (int64_t)sample.current_uA * delta_ms;
    const int64_t UAH_THRESHOLD = 3600000LL;   // 1 uAh = 3,600,000 uA·ms
    const int64_t whole_uah = charge_accum_uAms / UAH_THRESHOLD;
    charge_accum_uAms %= UAH_THRESHOLD;    // 保留带符号余数

    // ----- 能量积分 (uWh) -----
    // 1 uWh = 3.6e12 uA·uV·ms
    energy_accum_uvAms += (int64_t)sample.current_uA * sample.voltage_uv * delta_ms;
    const int64_t UWH_THRESHOLD = 3600000000000LL;   // 3.6e12
    const int64_t whole_uwh = energy_accum_uvAms / UWH_THRESHOLD;
    energy_accum_uvAms %= UWH_THRESHOLD;

    add_meter_totals(whole_uah, whole_uwh);

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
    if (!ulp_ina226_init()) {
        while (1) {
            ulp_lp_core_delay_us(MS_TO_US(3000));
        }
    }
    with_shared_lock_void([]() {
        ulp_state_p.ulp_state_bits.ulp_run = true;
    });
    while (1) {
        ina226_run();
        timer_run();

        //计算循环运行频率
        app_loop_every_ms(1000, [](){
            with_shared_lock_void([]() {
                core_run_freq_hz = loop_times - last_loop_times;
            });
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
