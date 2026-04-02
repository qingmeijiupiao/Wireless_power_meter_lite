#include "protect.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "global_state.h"

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



GlobalState& global_state_protect = get_global_state();

protect_threshold_t temperature_threshold ={
    .warning_threshold = 60.0f,
    .warning_recovery_threshold = 55.0f,
    .protect_threshold = 80.0f,
    .protect_recovery_threshold = 75.0f,
    .is_asc = true,
};

protect_threshold_t voltage_threshold ={
    .warning_threshold = 25.5f,
    .warning_recovery_threshold = 25.3f,
    .protect_threshold = 27.5f,
    .protect_recovery_threshold = 27.0f,
    .is_asc = true,
};

protect_threshold_t current_threshold ={
    .warning_threshold = 15.0f,
    .warning_recovery_threshold = 15.0f,
    .protect_threshold = 25.0f,
    .protect_recovery_threshold = 25.00f,
    .is_asc = true,
};

ProtectState_t check_now_state(protect_threshold_t threshold, ProtectState_t last_state, float now_value) {
    // 用lambda统一处理升序/降序的阈值比较
    auto is_triggered = [&](float value, float th) -> bool {
        return threshold.is_asc ? (value >= th) : (value <= th);
    };

    switch (last_state) {
        case PROTECT_STATE_NORMAL:
            // 直接越过保护阈值
            if (is_triggered(now_value, threshold.protect_threshold)) {
                PROTECT_LOGE("protect_threshold=%.3f, now_value=%.3f", threshold.protect_threshold, now_value);
                return PROTECT_STATE_PROTECT;
            }
            // 越过警告阈值
            if (is_triggered(now_value, threshold.warning_threshold)) {
                PROTECT_LOGW("warning_threshold=%.3f, now_value=%.3f", threshold.warning_threshold, now_value);
                return PROTECT_STATE_WARNING;
            }
            return PROTECT_STATE_NORMAL;

        case PROTECT_STATE_WARNING:
            // 继续恶化至保护阈值
            if (is_triggered(now_value, threshold.protect_threshold)) {
                PROTECT_LOGE("protect_threshold=%.3f, now_value=%.3f", threshold.protect_threshold, now_value);
                return PROTECT_STATE_PROTECT;
            }
            // 恢复到警告恢复阈值以下（利用反向比较）
            if (!is_triggered(now_value, threshold.warning_recovery_threshold)) {
                PROTECT_LOGI("warning_recovery_threshold=%.3f, now_value=%.3f", threshold.warning_recovery_threshold, now_value);
                return PROTECT_STATE_NORMAL;
            }
            return PROTECT_STATE_WARNING;

        case PROTECT_STATE_PROTECT:
            // 恢复到保护恢复阈值以下，退回警告状态（而非直接正常，需二次确认）
            if (!is_triggered(now_value, threshold.protect_recovery_threshold)) {
                PROTECT_LOGI("protect_recovery_threshold=%.3f, now_value=%.3f", threshold.protect_recovery_threshold, now_value);
                return PROTECT_STATE_WARNING;
            }
            return PROTECT_STATE_PROTECT;

        default:
            PROTECT_LOGE("unknown state %d", last_state);
            return PROTECT_STATE_NORMAL;
    }
}


void protect_task(void* pvParameters){
    auto ticks = xTaskGetTickCount();
    constexpr int protect_check_HZ = 10;
    while(1){
        //检查温度保护状态
        global_state_protect.protect_states.temperature_protect_state = check_now_state(temperature_threshold, global_state_protect.protect_states.temperature_protect_state, global_state_protect.NTC_temperature);

        //检查电压保护状态
        global_state_protect.protect_states.voltage_protect_state = check_now_state(voltage_threshold, global_state_protect.protect_states.voltage_protect_state, global_state_protect.voltage_uV / 1e6);

        //检查电流保护状态
        global_state_protect.protect_states.current_protect_state = check_now_state(current_threshold, global_state_protect.protect_states.current_protect_state, global_state_protect.current_nA / 1e9);
        
        xTaskDelayUntil(&ticks, configTICK_RATE_HZ / protect_check_HZ);
    }
}