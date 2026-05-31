#include "protect.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "global_state.h"
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

GlobalState& glb_states = get_global_state();

static int32_t to_milli(float value) {
    return static_cast<int32_t>(value * 1000.0f);
}

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

ProtectState_t check_now_state(protect_threshold_t threshold, ProtectState_t last_state, float now_value) {
    // 用lambda统一处理升序/降序的阈值比较
    auto is_triggered = [&](float value, float th) -> bool {
        return threshold.is_asc ? (value >= th) : (value <= th);
    };

    switch (last_state) {
        case PROTECT_STATE_NORMAL:
            // 直接越过保护阈值
            if (is_triggered(now_value, threshold.protect_threshold)) {
                PROTECT_LOGE("protect_threshold_milli=%ld, now_value_milli=%ld",
                             static_cast<long>(to_milli(threshold.protect_threshold)),
                             static_cast<long>(to_milli(now_value)));
                return PROTECT_STATE_PROTECT;
            }
            // 越过警告阈值
            if (is_triggered(now_value, threshold.warning_threshold)) {
                PROTECT_LOGW("warning_threshold_milli=%ld, now_value_milli=%ld",
                             static_cast<long>(to_milli(threshold.warning_threshold)),
                             static_cast<long>(to_milli(now_value)));
                return PROTECT_STATE_WARNING;
            }
            return PROTECT_STATE_NORMAL;

        case PROTECT_STATE_WARNING:
            // 继续恶化至保护阈值
            if (is_triggered(now_value, threshold.protect_threshold)) {
                PROTECT_LOGE("protect_threshold_milli=%ld, now_value_milli=%ld",
                             static_cast<long>(to_milli(threshold.protect_threshold)),
                             static_cast<long>(to_milli(now_value)));
                return PROTECT_STATE_PROTECT;
            }
            // 恢复到警告恢复阈值以下（利用反向比较）
            if (!is_triggered(now_value, threshold.warning_recovery_threshold)) {
                PROTECT_LOGI("warning_recovery_threshold_milli=%ld, now_value_milli=%ld",
                             static_cast<long>(to_milli(threshold.warning_recovery_threshold)),
                             static_cast<long>(to_milli(now_value)));
                return PROTECT_STATE_NORMAL;
            }
            return PROTECT_STATE_WARNING;

        case PROTECT_STATE_PROTECT:
            // 恢复到保护恢复阈值以下，退回警告状态（而非直接正常，需二次确认）
            if (!is_triggered(now_value, threshold.protect_recovery_threshold)) {
                PROTECT_LOGI("protect_recovery_threshold_milli=%ld, now_value_milli=%ld",
                             static_cast<long>(to_milli(threshold.protect_recovery_threshold)),
                             static_cast<long>(to_milli(now_value)));
                return PROTECT_STATE_WARNING;
            }
            return PROTECT_STATE_PROTECT;

        default:
            PROTECT_LOGE("unknown state %d", last_state);
            return PROTECT_STATE_NORMAL;
    }
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

void protect_set_bypassed(bool bypassed){
    glb_states.flags.bits.protect_bypassed = bypassed;
    PROTECT_LOGW("protect bypass %s", bypassed ? "enabled" : "disabled");
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

static bool _protect_init_ok = false;
void protect_task(void* pvParameters){
    auto ticks = xTaskGetTickCount();
    constexpr int protect_check_HZ = 20;
    auto& global_state_protects = glb_states.protect_states.states_bit; 
    ProtectState_t temp_state;
    ProtectState_t last_state;
    static bool first_check = true;

    while(1){
        //检查温度保护状态
        temp_state= check_now_state(temperature_threshold, global_state_protects.temperature_protect_state, glb_states.board_temperature/ 100.0f);
        if(temp_state != global_state_protects.temperature_protect_state){
            last_state = global_state_protects.temperature_protect_state;
            global_state_protects.temperature_protect_state = temp_state;
            for(auto& cb : protect_change_callbacks){
                cb(last_state, temp_state);
            }
        }

        //检查电压保护状态
        temp_state= check_now_state(high_voltage_threshold, global_state_protects.high_voltage_protect_state, glb_states.voltage_mV/ 1e3);
        if(temp_state != global_state_protects.high_voltage_protect_state){
            last_state = global_state_protects.high_voltage_protect_state;
            global_state_protects.high_voltage_protect_state = temp_state;
            for(auto& cb : protect_change_callbacks){
                cb(last_state, temp_state);
            }

        }
        
        temp_state= check_now_state(low_voltage_threshold, global_state_protects.low_voltage_protect_state, glb_states.voltage_mV/ 1e3);
        if(temp_state != global_state_protects.low_voltage_protect_state){
            last_state = global_state_protects.low_voltage_protect_state;
            global_state_protects.low_voltage_protect_state = temp_state;
            for(auto& cb : protect_change_callbacks){
                cb(last_state, temp_state);
            }
        }
        
        //检查电流保护状态
        temp_state= check_now_state(current_threshold, global_state_protects.current_protect_state, std::abs(glb_states.current_uA) / 1e6);
        if(temp_state != global_state_protects.current_protect_state){
            last_state = global_state_protects.current_protect_state;
            global_state_protects.current_protect_state = temp_state;
            for(auto& cb : protect_change_callbacks){
                cb(last_state, temp_state);
            }
        }

        if(first_check){
            first_check = false;
            _protect_init_ok = true;
            glb_states.flags.bits.protect_initialized = true;
            ESP_LOGI(PROTECT_LOG_TAG, "protect first check complete");
        }

        xTaskDelayUntil(&ticks, configTICK_RATE_HZ / protect_check_HZ);
    }
}

esp_err_t protect_init(){
    if(protect_task_handle){
        ESP_LOGW(PROTECT_LOG_TAG, "protect task already running");
        return ESP_OK;
    }
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
        glb_states.flags.bits.protect_initialized = false;
        vTaskDelete(protect_task_handle);
        protect_task_handle = nullptr;
    }else{
        ESP_LOGW(PROTECT_LOG_TAG, "protect task not running");
    }
    return ESP_OK;
}
