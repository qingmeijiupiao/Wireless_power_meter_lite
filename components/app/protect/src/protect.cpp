#include "protect.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "global_state.h"
#include "blackbox_service.h"
#include "HXC_NVS.h"
#include <cmath>
#include <vector>
#ifndef ENABLE_PROTECT_LOG
#define ENABLE_PROTECT_LOG 1
#endif

#if ENABLE_PROTECT_LOG == 1
#include "esp_log.h"
#define PROTECT_LOG_TAG "protect"
#define PROTECT_LOGE(fmt, ...)  ESP_LOGE(PROTECT_LOG_TAG, fmt, ##__VA_ARGS__)
#define PROTECT_LOGW(fmt, ...)  ESP_LOGW(PROTECT_LOG_TAG, fmt, ##__VA_ARGS__)
#define PROTECT_LOGI(fmt, ...)  ESP_LOGI(PROTECT_LOG_TAG, fmt, ##__VA_ARGS__)
#else
#define PROTECT_LOGE(fmt, ...)  (void)
#define PROTECT_LOGW(fmt, ...)  (void)
#define PROTECT_LOGI(fmt, ...)  (void)
#endif

TaskHandle_t protect_task_handle = nullptr;

// 保护模块直接读取共享运行状态，避免轮询路径重复获取单例引用。
GlobalState& glb_states = get_global_state();

/** @brief 将物理量转换为千分之一单位，供日志使用整数格式输出。 */
static int32_t to_milli(float value) {
    return static_cast<int32_t>(value * 1000.0f);
}

/**
 * @brief 检测输出 MOS 是否损坏。
 *
 * 当输出关闭但仍持续检测到不小于阈值的电流时，判定 MOS 可能损坏并打印一次错误日志。
 * 输出重新开启或电流恢复到阈值以下后，重新允许下一次故障上报。
 */
static void check_mos_fault() {
    constexpr int32_t mos_fault_current_threshold_uA = 10 * 1000;
    constexpr TickType_t mos_fault_output_settle_ticks = pdMS_TO_TICKS(500);
    constexpr TickType_t mos_fault_detection_ticks = pdMS_TO_TICKS(200);
    static TickType_t output_off_start_ticks = 0;
    static TickType_t detection_start_ticks = 0;
    static bool output_was_enabled = true;
    static bool detection_active = false;
    static bool fault_reported = false;

    const TickType_t now_ticks = xTaskGetTickCount();
    const int32_t current_uA = std::abs(glb_states.current_uA);
    // 输出开启时重置诊断；下一次关闭后先等待 INA226 平均采样窗口稳定。
    if (glb_states.flags.bits.output_enabled) {
        output_was_enabled = true;
        detection_active = false;
        fault_reported = false;
        return;
    }

    if (output_was_enabled) {
        output_was_enabled = false;
        output_off_start_ticks = now_ticks;
        detection_active = false;
        fault_reported = false;
        return;
    }

    if (now_ticks - output_off_start_ticks < mos_fault_output_settle_ticks ||
        current_uA < mos_fault_current_threshold_uA) {
        detection_active = false;
        fault_reported = false;
        return;
    }

    // 首次检测到异常电流时开始计时，过滤关断瞬态和采样波动。
    if (!detection_active) {
        detection_start_ticks = now_ticks;
        detection_active = true;
        return;
    }

    // 异常电流持续达到指定时间后仅上报一次，避免保护轮询持续刷屏。
    if (!fault_reported && now_ticks - detection_start_ticks >= mos_fault_detection_ticks) {
        PROTECT_LOGE("MOS fault detected: output is off but current remains, current_ma=%ld",
                     static_cast<long>(current_uA / 1000));
        fault_reported = true;
    }
}

/**
 * @brief 将已正式提交的保护状态变化写入黑匣子。
 *
 * 日志同时保留当前值、主要阈值、旁路状态、输出状态和 INA226 原始值，
 * 用于离线定位保护触发原因。
 */
static void append_state_change_event(const char* channel, ProtectState_t last_state, ProtectState_t new_state,
                                      float value, const protect_threshold_t& threshold) {
    const int current_raw = glb_states.current_register_raw;
    const unsigned voltage_raw = glb_states.voltage_register_raw;
    BlackboxService::append_event("protect: channel=%s state=%u->%u value_milli=%ld warn=%ld protect=%ld bypass=%u output=%u raw_i=%d raw_v=%u",
                                  channel,
                                  static_cast<unsigned>(last_state),
                                  static_cast<unsigned>(new_state),
                                  static_cast<long>(to_milli(value)),
                                  static_cast<long>(to_milli(threshold.warning_threshold)),
                                  static_cast<long>(to_milli(threshold.protect_threshold)),
                                  protect_is_bypassed() ? 1U : 0U,
                                  glb_states.flags.bits.output_enabled ? 1U : 0U,
                                  current_raw,
                                  voltage_raw);
}

// 以下运行期阈值会在首次访问时由 NVS 配置覆盖；声明值与默认配置保持一致。
protect_threshold_t temperature_threshold ={
    .warning_threshold = 60.0f,
    .warning_recovery_threshold = 55.0f,
    .protect_threshold = 80.0f,
    .protect_recovery_threshold = 75.0f,
    .is_asc = true,
};

protect_threshold_t high_voltage_threshold ={
    .warning_threshold = 25.5f,
    .warning_recovery_threshold = 25.3f,
    .protect_threshold = 27.5f,
    .protect_recovery_threshold = 27.0f,
    .is_asc = true,
};

protect_threshold_t low_voltage_threshold ={
    .warning_threshold = 6.6f,
    .warning_recovery_threshold = 7.2f,
    .protect_threshold = 4.7f,
    .protect_recovery_threshold = 5.0f,
    .is_asc = false,
};


protect_threshold_t current_threshold ={
    .warning_threshold = 15.0f,
    .warning_recovery_threshold = 15.0f,
    .protect_threshold = 25.0f,
    .protect_recovery_threshold = 25.00f,
    .is_asc = true,
};

/** @brief 四个保护通道的完整持久化配置。 */
struct protect_config_t {
    protect_threshold_t temperature;
    protect_threshold_t high_voltage;
    protect_threshold_t low_voltage;
    protect_threshold_t current;
};

// NVS 中没有有效数据时使用的出厂默认值。
static constexpr protect_config_t DEFAULT_PROTECT_CONFIG = {
    .temperature = {
        .warning_threshold = 60.0f,
        .warning_recovery_threshold = 55.0f,
        .protect_threshold = 80.0f,
        .protect_recovery_threshold = 75.0f,
        .is_asc = true,
    },
    .high_voltage = {
        .warning_threshold = 25.5f,
        .warning_recovery_threshold = 25.3f,
        .protect_threshold = 27.5f,
        .protect_recovery_threshold = 27.0f,
        .is_asc = true,
    },
    .low_voltage = {
        .warning_threshold = 6.6f,
        .warning_recovery_threshold = 7.2f,
        .protect_threshold = 4.7f,
        .protect_recovery_threshold = 5.0f,
        .is_asc = false,
    },
    .current = {
        .warning_threshold = 15.0f,
        .warning_recovery_threshold = 15.0f,
        .protect_threshold = 25.0f,
        .protect_recovery_threshold = 25.0f,
        .is_asc = true,
    },
};

// 四组阈值整体保存，避免逐字段更新造成通道配置不一致。
static HXC::NVS_DATA<protect_config_t> protect_config_data("PROTECT_CFG", DEFAULT_PROTECT_CONFIG);
static bool protect_config_loaded = false;

/**
 * @brief 校验单个保护通道的阈值顺序和数值范围。
 *
 * 升序通道要求：告警恢复 <= 告警 <= 保护恢复 <= 保护。
 * 降序通道要求：保护 <= 保护恢复 <= 告警 <= 告警恢复。
 */
static bool validate_threshold(const protect_threshold_t& threshold) {
    if (!std::isfinite(threshold.warning_threshold) ||
        !std::isfinite(threshold.warning_recovery_threshold) ||
        !std::isfinite(threshold.protect_threshold) ||
        !std::isfinite(threshold.protect_recovery_threshold) ||
        threshold.warning_threshold < 0.0f ||
        threshold.warning_recovery_threshold < 0.0f ||
        threshold.protect_threshold < 0.0f ||
        threshold.protect_recovery_threshold < 0.0f) {
        return false;
    }

    if (threshold.is_asc) {
        return threshold.warning_recovery_threshold <= threshold.warning_threshold &&
               threshold.warning_threshold <= threshold.protect_recovery_threshold &&
               threshold.protect_recovery_threshold <= threshold.protect_threshold;
    }
    return threshold.protect_threshold <= threshold.protect_recovery_threshold &&
           threshold.protect_recovery_threshold <= threshold.warning_threshold &&
           threshold.warning_threshold <= threshold.warning_recovery_threshold;
}

/** @brief 将一组配置写入运行期阈值变量。 */
static void apply_protect_config(const protect_config_t& config) {
    temperature_threshold = config.temperature;
    high_voltage_threshold = config.high_voltage;
    low_voltage_threshold = config.low_voltage;
    current_threshold = config.current;
}

/** @brief 校验保护配置，避免损坏的 NVS 数据进入运行期。 */
static bool validate_protect_config(const protect_config_t& config) {
    return config.temperature.is_asc == 1 &&
           config.high_voltage.is_asc == 1 &&
           config.low_voltage.is_asc == 0 &&
           config.current.is_asc == 1 &&
           validate_threshold(config.temperature) &&
           validate_threshold(config.high_voltage) &&
           validate_threshold(config.low_voltage) &&
           validate_threshold(config.current);
}

/** @brief 首次使用时从 NVS 加载保护阈值，非法配置回退为默认值。 */
static void ensure_protect_config_loaded() {
    if (protect_config_loaded) {
        return;
    }

    protect_config_t config = protect_config_data.read();
    if (!validate_protect_config(config)) {
        PROTECT_LOGE("invalid protect thresholds in NVS, restoring defaults");
        config = DEFAULT_PROTECT_CONFIG;
        const esp_err_t save_err = protect_config_data.set(config);
        if (save_err != ESP_OK) {
            PROTECT_LOGE("failed to restore default thresholds: %s", esp_err_to_name(save_err));
        }
    }
    apply_protect_config(config);
    protect_config_loaded = true;
}

/** @brief 分行输出并持久化记录单个通道的阈值，避免单条日志过长。 */
static void log_threshold_values(const char* channel, const protect_threshold_t& threshold) {
    PROTECT_LOGI("threshold channel=%s warn_milli=%ld warn_rec_milli=%ld",
                 channel,
                 static_cast<long>(to_milli(threshold.warning_threshold)),
                 static_cast<long>(to_milli(threshold.warning_recovery_threshold)));
    PROTECT_LOGI("threshold channel=%s protect_milli=%ld protect_rec_milli=%ld",
                 channel,
                 static_cast<long>(to_milli(threshold.protect_threshold)),
                 static_cast<long>(to_milli(threshold.protect_recovery_threshold)));
    BlackboxService::append_text_event("protect: threshold channel=%s warn_milli=%ld warn_rec_milli=%ld",
                                       channel,
                                       static_cast<long>(to_milli(threshold.warning_threshold)),
                                       static_cast<long>(to_milli(threshold.warning_recovery_threshold)));
    BlackboxService::append_text_event("protect: threshold channel=%s protect_milli=%ld protect_rec_milli=%ld",
                                       channel,
                                       static_cast<long>(to_milli(threshold.protect_threshold)),
                                       static_cast<long>(to_milli(threshold.protect_recovery_threshold)));
}

/** @brief 输出并持久化记录保护模块启动时实际使用的阈值。 */
static void log_initial_thresholds() {
    for (uint8_t i = 0; i < protect_get_channel_count(); ++i) {
        protect_channel_info_t info = {};
        if (protect_get_channel_info(i, &info)) {
            log_threshold_values(info.name, info.threshold);
        }
    }
}

/**
 * @brief 根据当前值计算通道候选状态。
 *
 * @note 本函数只计算候选状态，不直接写回全局状态。候选状态还需要经过
 *       debounce_protect_state() 的固定时间迟滞后才能正式提交。
 */
ProtectState_t check_now_state(protect_threshold_t threshold, ProtectState_t last_state, float now_value) {
    // 用lambda统一处理升序/降序的阈值比较
    auto is_triggered = [&](float value, float th) -> bool {
        return threshold.is_asc ? (value >= th) : (value <= th);
    };

    switch (last_state) {
        case PROTECT_STATE_NORMAL:
            // 直接越过保护阈值
            if (is_triggered(now_value, threshold.protect_threshold)) {
                return PROTECT_STATE_PROTECT;
            }
            // 越过警告阈值
            if (is_triggered(now_value, threshold.warning_threshold)) {
                return PROTECT_STATE_WARNING;
            }
            return PROTECT_STATE_NORMAL;

        case PROTECT_STATE_WARNING:
            // 继续恶化至保护阈值
            if (is_triggered(now_value, threshold.protect_threshold)) {
                return PROTECT_STATE_PROTECT;
            }
            // 恢复到警告恢复阈值以下（利用反向比较）
            if (!is_triggered(now_value, threshold.warning_recovery_threshold)) {
                return PROTECT_STATE_NORMAL;
            }
            return PROTECT_STATE_WARNING;

        case PROTECT_STATE_PROTECT:
            // 恢复到保护恢复阈值以下，退回警告状态（而非直接正常，需二次确认）
            if (!is_triggered(now_value, threshold.protect_recovery_threshold)) {
                return PROTECT_STATE_WARNING;
            }
            return PROTECT_STATE_PROTECT;

        default:
            PROTECT_LOGE("unknown state %d", last_state);
            return PROTECT_STATE_NORMAL;
    }
}

// 所有保护通道共用的编译期时间迟滞，不保存到 NVS，也不提供运行期修改入口。
constexpr uint32_t protect_state_change_delay_ms = 200;
constexpr TickType_t protect_state_change_delay_ticks = pdMS_TO_TICKS(protect_state_change_delay_ms);

/** @brief 单个通道正在等待提交的候选状态及其起始时间。 */
struct protect_pending_state_t {
    ProtectState_t state;
    TickType_t start_ticks;
    bool active;
};

// OTP、OVP、UVP、OCP 四个通道分别维护独立计时，互不影响。
static protect_pending_state_t protect_pending_states[4] = {};

/** @brief 清除所有通道尚未提交的状态切换计时。 */
static void reset_pending_protect_states() {
    for (auto& pending : protect_pending_states) {
        pending = {};
    }
}

/**
 * @brief 对保护状态切换执行固定 200ms 时间迟滞。
 *
 * 候选状态必须连续保持到编译期固定延迟后才正式提交。候选状态恢复为当前状态，
 * 或在等待期间变化为另一状态时，原计时立即取消。
 */
static ProtectState_t debounce_protect_state(uint8_t channel, ProtectState_t current_state, ProtectState_t candidate_state) {
    if (channel >= sizeof(protect_pending_states) / sizeof(protect_pending_states[0])) {
        return current_state;
    }
    protect_pending_state_t& pending = protect_pending_states[channel];
    if (candidate_state == current_state) {
        pending.active = false;
        return current_state;
    }

    const TickType_t now_ticks = xTaskGetTickCount();
    if (!pending.active || pending.state != candidate_state) {
        pending.state = candidate_state;
        pending.start_ticks = now_ticks;
        pending.active = true;
        return current_state;
    }

    if (now_ticks - pending.start_ticks < protect_state_change_delay_ticks) {
        return current_state;
    }

    pending.active = false;
    PROTECT_LOGI("state change channel=%u state=%u->%u delay_ms=%lu",
                 static_cast<unsigned>(channel),
                 static_cast<unsigned>(current_state),
                 static_cast<unsigned>(candidate_state),
                 static_cast<unsigned long>(protect_state_change_delay_ms));
    return candidate_state;
}

static std::vector<std::function<void(ProtectState_t, ProtectState_t)>> protect_change_callbacks;

void add_on_protect_change_callback(std::function<void(ProtectState_t last_state, ProtectState_t new_state)> cb){
    protect_change_callbacks.push_back(cb);
}

bool have_protect(){
    return protect_has_active_fault();
}

bool protect_has_active_fault(){
    auto& global_state_protects = glb_states.protect_states.states_bit; 
    return global_state_protects.temperature_protect_state == PROTECT_STATE_PROTECT || 
        global_state_protects.high_voltage_protect_state == PROTECT_STATE_PROTECT || 
        global_state_protects.low_voltage_protect_state == PROTECT_STATE_PROTECT ||  
        global_state_protects.current_protect_state == PROTECT_STATE_PROTECT;
}

void protect_set_bypassed(bool bypassed, const char* source){
    source = source == nullptr ? "unknown" : source;
    if (glb_states.flags.bits.protect_bypassed == bypassed) {
        return;
    }
    glb_states.flags.bits.protect_bypassed = bypassed;
    PROTECT_LOGW("bypass source=%s state=%u", source, bypassed ? 1U : 0U);
    BlackboxService::append_event("protect: bypass source=%s state=%u active_fault=%u output=%u",
                                  source,
                                  bypassed ? 1U : 0U,
                                  protect_has_active_fault() ? 1U : 0U,
                                  glb_states.flags.bits.output_enabled ? 1U : 0U);
}

bool protect_is_bypassed(){
    return glb_states.flags.bits.protect_bypassed;
}

bool protect_should_block_output(){
    return !protect_is_bypassed() && protect_has_active_fault();
}

uint8_t protect_get_channel_count(){
    return 4;
}

bool protect_get_channel_info(uint8_t index, protect_channel_info_t* info){
    if(info == nullptr){
        return false;
    }
    ensure_protect_config_loaded();

    auto& global_state_protects = glb_states.protect_states.states_bit;
    switch(index){
        case 0:
            *info = {
                .name = "OTP",
                .unit = "C",
                .now_value = glb_states.board_temperature / 100.0f,
                .state = global_state_protects.temperature_protect_state,
                .threshold = temperature_threshold,
            };
            return true;
        case 1:
            *info = {
                .name = "OVP",
                .unit = "V",
                .now_value = glb_states.voltage_mV / 1000.0f,
                .state = global_state_protects.high_voltage_protect_state,
                .threshold = high_voltage_threshold,
            };
            return true;
        case 2:
            *info = {
                .name = "UVP",
                .unit = "V",
                .now_value = glb_states.voltage_mV / 1000.0f,
                .state = global_state_protects.low_voltage_protect_state,
                .threshold = low_voltage_threshold,
            };
            return true;
        case 3:
            *info = {
                .name = "OCP",
                .unit = "A",
                .now_value = std::abs(glb_states.current_uA) / 1000000.0f,
                .state = global_state_protects.current_protect_state,
                .threshold = current_threshold,
            };
            return true;
        default:
            return false;
    }
}

esp_err_t protect_set_channel_threshold(uint8_t index, const protect_threshold_t& threshold, const char* source) {
    ensure_protect_config_loaded();
    protect_channel_info_t info = {};
    if (!protect_get_channel_info(index, &info) ||
        threshold.is_asc != info.threshold.is_asc ||
        !validate_threshold(threshold)) {
        return ESP_ERR_INVALID_ARG;
    }

    // 复制完整配置，仅替换目标通道，确保单次 NVS 写入得到一致快照。
    protect_config_t config = {
        .temperature = temperature_threshold,
        .high_voltage = high_voltage_threshold,
        .low_voltage = low_voltage_threshold,
        .current = current_threshold,
    };
    protect_threshold_t* target = nullptr;
    switch (index) {
        case 0: target = &config.temperature; break;
        case 1: target = &config.high_voltage; break;
        case 2: target = &config.low_voltage; break;
        case 3: target = &config.current; break;
        default: return ESP_ERR_INVALID_ARG;
    }
    *target = threshold;

    const esp_err_t save_err = protect_config_data.set(config);
    if (save_err != ESP_OK) {
        PROTECT_LOGE("failed to persist thresholds: %s", esp_err_to_name(save_err));
        return save_err;
    }
    apply_protect_config(config);
    source = source == nullptr ? "unknown" : source;
    PROTECT_LOGI("threshold updated source=%s channel=%s", source, info.name);
    BlackboxService::append_text_event("protect: threshold_updated source=%s channel=%s", source, info.name);
    log_threshold_values(info.name, threshold);
    return ESP_OK;
}

static bool _protect_init_ok = false;

/**
 * @brief 保护轮询任务。
 *
 * 以 20Hz 依次检查 OTP、OVP、UVP、OCP。每个通道先计算候选状态，再执行
 * 固定 200ms 时间迟滞；正式切换后记录黑匣子并通知输出控制等订阅模块。
 */
void protect_task(void* pvParameters){
    auto ticks = xTaskGetTickCount();
    constexpr int protect_check_HZ = 20;
    auto& global_state_protects = glb_states.protect_states.states_bit; 
    ProtectState_t temp_state;
    ProtectState_t last_state;
    static bool first_check = true;

    while(1){
        //检查温度保护状态
        temp_state = debounce_protect_state(0, global_state_protects.temperature_protect_state,
            check_now_state(temperature_threshold, global_state_protects.temperature_protect_state, glb_states.board_temperature / 100.0f));
        if(temp_state != global_state_protects.temperature_protect_state){
            last_state = global_state_protects.temperature_protect_state;
            global_state_protects.temperature_protect_state = temp_state;
            append_state_change_event("OTP", last_state, temp_state, glb_states.board_temperature / 100.0f, temperature_threshold);
            for(auto& cb : protect_change_callbacks){
                cb(last_state, temp_state);
            }
        }

        //检查电压保护状态
        temp_state = debounce_protect_state(1, global_state_protects.high_voltage_protect_state,
            check_now_state(high_voltage_threshold, global_state_protects.high_voltage_protect_state, glb_states.voltage_mV / 1e3));
        if(temp_state != global_state_protects.high_voltage_protect_state){
            last_state = global_state_protects.high_voltage_protect_state;
            global_state_protects.high_voltage_protect_state = temp_state;
            append_state_change_event("OVP", last_state, temp_state, glb_states.voltage_mV / 1e3, high_voltage_threshold);
            for(auto& cb : protect_change_callbacks){
                cb(last_state, temp_state);
            }

        }
        
        temp_state = debounce_protect_state(2, global_state_protects.low_voltage_protect_state,
            check_now_state(low_voltage_threshold, global_state_protects.low_voltage_protect_state, glb_states.voltage_mV / 1e3));
        if(temp_state != global_state_protects.low_voltage_protect_state){
            last_state = global_state_protects.low_voltage_protect_state;
            global_state_protects.low_voltage_protect_state = temp_state;
            append_state_change_event("UVP", last_state, temp_state, glb_states.voltage_mV / 1e3, low_voltage_threshold);
            for(auto& cb : protect_change_callbacks){
                cb(last_state, temp_state);
            }
        }
        
        //检查电流保护状态
        temp_state = debounce_protect_state(3, global_state_protects.current_protect_state,
            check_now_state(current_threshold, global_state_protects.current_protect_state, std::abs(glb_states.current_uA) / 1e6));
        if(temp_state != global_state_protects.current_protect_state){
            last_state = global_state_protects.current_protect_state;
            global_state_protects.current_protect_state = temp_state;
            append_state_change_event("OCP", last_state, temp_state, std::abs(glb_states.current_uA) / 1e6, current_threshold);
            for(auto& cb : protect_change_callbacks){
                cb(last_state, temp_state);
            }
        }

        // MOS 损坏诊断独立于四类保护状态，只上报日志，不修改保护状态。
        check_mos_fault();

        if(first_check){
            first_check = false;
            _protect_init_ok = true;
            glb_states.flags.bits.protect_initialized = true;
            ESP_LOGI(PROTECT_LOG_TAG, "protect first check complete");
            BlackboxService::append_event("protect: first_check_complete");
        }

        xTaskDelayUntil(&ticks, configTICK_RATE_HZ / protect_check_HZ);
    }
}

esp_err_t protect_init(){
    if(protect_task_handle){
        ESP_LOGW(PROTECT_LOG_TAG, "protect task already running");
        return ESP_OK;
    }
    // 启动任务前先确定实际阈值，确保首次轮询和启动日志使用同一组配置。
    ensure_protect_config_loaded();
    log_initial_thresholds();
    reset_pending_protect_states();
    constexpr uint32_t protect_task_stack_size = 4096;
    if (xTaskCreate(protect_task, "protect_task", protect_task_stack_size, nullptr, 5, &protect_task_handle) != pdPASS) {
        protect_task_handle = nullptr;
        ESP_LOGE(PROTECT_LOG_TAG, "failed to create protect task");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
bool protect_init_ok(){
    return _protect_init_ok;
}
esp_err_t protect_deinit(){
    if(protect_task_handle){
        glb_states.protect_states.protect_states_raw = 0; //清除保护状态
        reset_pending_protect_states();
        glb_states.flags.bits.protect_initialized = false;
        vTaskDelete(protect_task_handle);
        protect_task_handle = nullptr;
        BlackboxService::append_event("protect: deinit");
    }else{
        ESP_LOGW(PROTECT_LOG_TAG, "protect task not running");
    }
    return ESP_OK;
}
