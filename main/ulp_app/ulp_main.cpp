#include "ulp_lp_core.h"
#include "ulp_lp_core_critical_section_shared.h"
#include "ulp_lp_core_utils.h"
#include <cstddef>
#include <cstdlib>
#include "ina226.hpp"
#include "ulp_state.h"
#include "ulp_Interp.hpp"
#include "../../components/app/current_calibration/include/CurrentCalib.h"


constexpr int voltage_scale = 1250; //电压校准系数，根据手册计算得到，无需校准

constexpr uint32_t LP_CPU_FREQ_HZ = 20000000;
constexpr uint32_t current_dead_zone_uA = 5000; //电流死区，单位uA

#define MS_TO_US(ms) ((ms) * 1000)

#define LP_VAR __attribute__((section(".rtc.bss")))
/* 定义共享变量，存放在RTC内存中（.rtc.bss段） */
volatile uint32_t      ulp_state                     LP_VAR;
ulp_lp_core_spinlock_t shared_lock                   LP_VAR;
volatile uint32_t      log_data                      LP_VAR;
volatile uint32_t      voltage_uv                    LP_VAR;
volatile uint16_t      voltage_register_raw          LP_VAR;
volatile int32_t       current_uA                    LP_VAR;
volatile int16_t       shunt_register_raw            LP_VAR;
volatile uint16_t      ina226_manufacturer_id        LP_VAR;
volatile int32_t       Board_temperature             LP_VAR; //单位0.01℃
volatile int64_t       meter_uah                     LP_VAR; //单位uAh
volatile int64_t       meter_uwh                     LP_VAR; //单位uWh
volatile CurrentCalib::params_t current_calib_params LP_VAR;

volatile uint32_t now_time_ms = 0;
UlpNonEquidistantInterp<int16_t,int16_t,6> current_interp;
ULP_CORE_STATE& ulp_state_p = *(ULP_CORE_STATE*)&(ulp_state);
// 计算过程使用 LP 本地副本，避免每次采样长时间占用跨核锁。
CurrentCalib::params_t local_current_calib_params;

/**
 * @brief 在 LP/HP 共享自旋锁保护下执行无返回值的短操作。
 *
 * @tparam F 可调用对象类型。
 * @param action 临界区内执行的操作。
 *
 * @note 用于原子更新共享状态位、测量值和 64 位累计量。
 */
template<typename F>
void with_shared_lock_void(F&& action) {
    ulp_lp_core_enter_critical(&shared_lock);
    action();
    ulp_lp_core_exit_critical(&shared_lock);
}

/**
 * @brief 发布一组完整的 INA226 采样结果到 RTC 共享区。
 *
 * @param new_voltage_register_raw INA226 总线电压寄存器原始值。
 * @param new_shunt_register_raw INA226 分流电压寄存器原始值。
 * @param new_voltage_uv 换算后的总线电压，单位 uV。
 * @param new_current_uA 补偿后的电流，单位 uA。
 *
 * @note 电压、电流和原始寄存器值必须同批次提交，HP 核才不会读到混合样本。
 */
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
        ulp_state_p.ulp_state_bits.ulp_ina226_read_timeout = false;
    });
}

/**
 * @brief 从 RTC 共享区复制电流校准参数。
 *
 * @return 当前 HP 核写入的电流校准参数副本。
 *
 * @note `current_calib_params` 位于 RTC 共享内存且带 volatile，逐字段复制后再在锁外重建插值表。
 */
static CurrentCalib::params_t read_current_calib_params() {
    // RTC 参数带 volatile，逐字段复制后再在锁外重建插值表。
    CurrentCalib::params_t params = {};
    with_shared_lock_void([&]() {
        params.current_base_K = current_calib_params.current_base_K;
        for (size_t i = 0; i < sizeof(params.points) / sizeof(params.points[0]); ++i) {
            params.points[i].register_value = current_calib_params.points[i].register_value;
            params.points[i].offset_current_100uA = current_calib_params.points[i].offset_current_100uA;
        }
        params.temperature_K = current_calib_params.temperature_K;
    });
    return params;
}

constexpr uint32_t INA226_READ_TIMEOUT_MS = 1000;

/**
 * @brief 执行一次 INA226 采样轮询并发布成功样本。
 *
 * @note 当前策略保持原行为：读寄存器失败或转换未完成时只检查 1 秒超时；
 *       超时后清零电压/电流并置位 `ulp_ina226_read_timeout`。
 */
void ina226_run(){
    static uint32_t last_success_ms = 0;
    const auto mark_read_timeout_if_needed = [&]() {
        // 只在本函数内判断连续采样失败时长，避免把 INA226 私有状态暴露为文件级变量。
        if ((now_time_ms - last_success_ms) <= INA226_READ_TIMEOUT_MS) {
            return;
        }

        // 采样失效后不再暴露陈旧值；当前保护链路会把 0V 当作 UVP 处理。
        with_shared_lock_void([]() {
            voltage_uv = 0;
            current_uA = 0;
            ulp_state_p.ulp_state_bits.ulp_ina226_read_timeout = true;
        });
    };

    uint16_t mask_enable = 0;
    if (INA226::read_register(INA226::Register_enum::INA226_MASK_ENABLE, &mask_enable) != ESP_OK) {
        mark_read_timeout_if_needed();
        return;
    }
    if(!(mask_enable & (1 << 3))){ //CNVR位为0，说明没有转换完成
        mark_read_timeout_if_needed();
        return;
    }

    uint16_t new_voltage_register_raw = 0;
    int16_t new_shunt_register_raw = 0;
    /* 读取电压寄存器 */
    if (INA226::read_register(INA226::Register_enum::INA226_BUS_VOLTAGE, &new_voltage_register_raw) != ESP_OK) {
        mark_read_timeout_if_needed();
        return;
    }

    /* 读取电流寄存器 */
    if (INA226::read_register(INA226::Register_enum::INA226_SHUNT_VOLTAGE,
                              (uint16_t*)&new_shunt_register_raw) != ESP_OK) {
        mark_read_timeout_if_needed();
        return;
    }

    const uint32_t new_voltage_uv = new_voltage_register_raw * voltage_scale;
    int32_t new_current_uA = 0;
    int32_t board_temperature = 0;
    with_shared_lock_void([&]() {
        board_temperature = Board_temperature;
    });
    if(std::abs((int32_t)new_shunt_register_raw * local_current_calib_params.current_base_K) < current_dead_zone_uA){
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
    last_success_ms = now_time_ms;
}

/**
 * @brief 初始化 INA226 并等待首个有效电压样本。
 *
 * @return true 初始化、配置和首个样本读取成功；false 任一 I2C 操作失败。
 *
 * @note 成功后置位 `ulp_ina226_init_ok`，失败时置位 `ulp_i2c_init_err` 供 HP 核诊断。
 */
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
        bool sample_ready = false;
        with_shared_lock_void([&]() {
            sample_ready = voltage_uv != 0;
        });
        if (sample_ready) {
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
 * @brief 加载电流校准参数并重建 LP 本地插值表。
 *
 * @note 校准参数从 RTC 共享区复制到 LP 本地副本，运行期采样不再频繁持锁访问共享参数。
 */
void load_current_calib_params(){
    local_current_calib_params = read_current_calib_params();
    for(int i = 0;i<sizeof(local_current_calib_params.points)/sizeof(local_current_calib_params.points[0]);i++){
        current_interp.set_point(i,local_current_calib_params.points[i].register_value,local_current_calib_params.points[i].offset_current_100uA);
    }
    current_interp.finish_load();
}
    
/**
 * @brief 更新 LP Core 内部毫秒计数。
 *
 * @note `ulp_lp_core_get_cpu_cycles()` 返回 CPU 周期计数，约 215 秒回绕一次；
 *       本函数将原始周期计数展开为持续递增的 `now_time_ms`。
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
 * @brief 检查 HP 核是否请求重新加载电流校准参数。
 *
 * @note 读取并清除 `ulp_reload_calib_params` 必须在同一临界区完成，避免重复处理或丢失请求。
 */
void check_reload_current_calib_params(){
    bool reload = false;
    with_shared_lock_void([&]() {
        reload = ulp_state_p.ulp_state_bits.ulp_reload_calib_params;
        ulp_state_p.ulp_state_bits.ulp_reload_calib_params = false;
    });
    if(reload){
        load_current_calib_params();
    }
}


/**
 * @brief 按当前采样值积分电量和能量累计值。
 *
 * @note 电荷单位为 uAh，能量单位为 uWh；内部保留带符号余数，避免低电流下积分量被截断。
 */ 
static int64_t charge_accum_uAms = 0;   // 电荷累加器，单位 uA·ms
static int64_t energy_accum_uvAms = 0;  // 能量乘积累加器，单位 uA·uV·ms
void update_meter() {
    static uint32_t last_run_ms = 0;
    uint32_t delta_ms = now_time_ms - last_run_ms;   // 不处理回绕，直接减
    if (delta_ms == 0) return;

    // ----- 电量积分 (uAh) -----
    int32_t sample_current_uA = 0;
    uint32_t sample_voltage_uv = 0;
    // 电流和电压必须在同一临界区读取，避免电量积分组合到不同批次样本。
    with_shared_lock_void([&]() {
        sample_current_uA = current_uA;
        sample_voltage_uv = voltage_uv;
    });

    charge_accum_uAms += (int64_t)sample_current_uA * delta_ms;
    const int64_t UAH_THRESHOLD = 3600000LL;   // 1 uAh = 3,600,000 uA·ms
    const int64_t whole_uah = charge_accum_uAms / UAH_THRESHOLD;
    charge_accum_uAms %= UAH_THRESHOLD;    // 保留带符号余数

    // ----- 能量积分 (uWh) -----
    // 1 uWh = 3.6e12 uA·uV·ms
    energy_accum_uvAms += (int64_t)sample_current_uA * sample_voltage_uv * delta_ms;
    const int64_t UWH_THRESHOLD = 3600000000000LL;   // 3.6e12
    const int64_t whole_uwh = energy_accum_uvAms / UWH_THRESHOLD;
    energy_accum_uvAms %= UWH_THRESHOLD;

    if (whole_uah != 0 || whole_uwh != 0) {
        // 两个 64 位累计值需要在同一临界区提交，供 HP 核读取一致快照。
        with_shared_lock_void([=]() {
            meter_uah += whole_uah;
            meter_uwh += whole_uwh;
        });
    }

    last_run_ms = now_time_ms;
}

/**
 * @brief 按固定毫秒间隔执行一次回调。
 *
 * @tparam F 可调用对象类型；每个模板实例拥有独立的 `last_run_ms`。
 * @param interval_ms 执行间隔，单位 ms。
 * @param action 到期后执行的回调。
 *
 * @note 当前实现使用 `>` 判断，因此实际间隔会略大于 `interval_ms`。
 */
template<typename F>
void app_loop_every_ms(uint32_t interval_ms, F&& action) {
    static uint32_t last_run_ms = 0;
    if ((now_time_ms - last_run_ms) > interval_ms) {
        last_run_ms = now_time_ms;
        action();
    }
}

/**
 * @brief LP Core 应用入口。
 *
 * @return 不返回；初始化失败或主循环都会常驻运行。
 *
 * @note 主循环持续轮询 INA226、维护毫秒计数、处理校准重载并执行电量积分。
 */
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

        //检查是否需要重新加载校准参数
        app_loop_every_ms(20,check_reload_current_calib_params);

        // 电量积分
        app_loop_every_ms(10,update_meter);
    }
}
