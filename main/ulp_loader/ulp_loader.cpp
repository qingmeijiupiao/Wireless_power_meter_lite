/*
 * @Description: LP 核加载器
 * @Author: qingmeijiupiao
 * @version: 1.0.0
 * @Date: 2026-04-20 00:48:01
 * @LastEditTime: 2026-04-30 18:29:00
 */
#include "ulp_loader.h"
#include "ulp_lp_core.h"
#include "ulp_main.h"
#include "esp_log.h"
#include "esp_task.h"
#include "lp_core_i2c.h"
#include "ulp_app/ulp_state.h"
#include "soc/lp_clkrst_reg.h"
#include "soc/lp_clkrst_struct.h"
#include "current_calibration.h"
#include "global_state.h"
#include "blackbox_service.h"
const char *LPTAG = "LP_CORE";

ulp_lp_core_cfg_t lp_core_init_cfg={
    .wakeup_source = ULP_LP_CORE_WAKEUP_SOURCE_HP_CPU,
    .lp_timer_sleep_duration_us = 0,
};

const lp_core_i2c_cfg_t i2c_cfg={
    .i2c_pin_cfg = {
        .sda_io_num = gpio_num_t::GPIO_NUM_6,
        .scl_io_num = gpio_num_t::GPIO_NUM_7,
        .sda_pullup_en = true,
        .scl_pullup_en = true,
    },
    .i2c_timing_cfg = {
        .clk_speed_hz = 400000,
    },
    .i2c_src_clk = LP_I2C_SCLK_DEFAULT,
};

HXC::NVS_DATA<CurrentCalib::params_t> CurrentCalib::params_data("CUR_CAL", CurrentCalib::DEFAULT);
CurrentCalib::params_t* ulp_calib_params = reinterpret_cast<CurrentCalib::params_t*>(ulp_current_calib_params);

extern "C" {
    extern const uint8_t bin_start[] asm("_binary_ulp_main_bin_start");
    extern const uint8_t bin_end[]   asm("_binary_ulp_main_bin_end");
}

ULP_CORE_STATE& ulp_state = *(ULP_CORE_STATE*)&(ulp_ulp_state);

/**
 * @brief : 打印 LP 核日志
 * @return  {*}
 * @param {void*} arg
 */
void print_lp_core_log_task(void* arg){
    while (1){
        if (ulp_state.ulp_state_bits.ulp_have_log){
            ESP_LOGI(LPTAG, "lp core log: %ld", ulp_log_data);
            ulp_state.ulp_state_bits.ulp_have_log = false;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

/**
 * @brief : 加载校准参数到 LP 核共享变量
 * @return  {*}
 * @param {bool} need_flag 是否需要设置校准参数标志位
 */
void load_current_calib_params(bool need_flag = true){
    *ulp_calib_params = CurrentCalib::params_data.read();
    BlackboxService::append_event("lp: calib_loaded base_k=%u temperature_k=%d reload=%u",
                                  static_cast<unsigned>(ulp_calib_params->current_base_K),
                                  ulp_calib_params->temperature_K,
                                  need_flag ? 1U : 0U);
    if(need_flag){
        ulp_state.ulp_state_bits.ulp_reload_calib_params = true;
    }
}


esp_err_t LP_Core_Load(void){
    ESP_LOGI(LPTAG, "main core start init lp core...");
    BlackboxService::append_event("lp: init_start i2c_hz=%lu",
                                  static_cast<unsigned long>(i2c_cfg.i2c_timing_cfg.clk_speed_hz));
    ulp_state.ulp_state_raw = 0; // 初始化 LP 核状态
    LP_CLKRST.lp_clk_conf.fast_clk_sel = 1; //IDF 6.0版本默认是内部RC时钟(17.5MHz)，且没有API可以切换到外部时钟源，需要手动操作寄存器切换到外部时钟源(20MHz)

    ESP_LOGI(LPTAG, "main core start init i2c...");
    ESP_ERROR_CHECK(lp_core_i2c_master_init(LP_I2C_NUM_0, &i2c_cfg));
    ESP_LOGI(LPTAG, "main core init i2c success...");

    // 加载 LP 核二进制文件
    ESP_ERROR_CHECK(ulp_lp_core_load_binary(bin_start, bin_end - bin_start));
    ESP_LOGI(LPTAG, "lp core load binary success...");


    ESP_ERROR_CHECK(ulp_lp_core_run(&lp_core_init_cfg)); 
    load_current_calib_params(false);
    int32_t timeout = 600;
    while (timeout-=10){
        if(ulp_state.ulp_state_bits.ulp_run && ulp_state.ulp_state_bits.ulp_ina226_init_ok){
            break;
        }
        vTaskDelay(10/ portTICK_PERIOD_MS);
    }

    if(ulp_state.ulp_state_bits.ulp_i2c_init_err){
        ESP_LOGE(LPTAG, "INA226 unavailable: communication failed");
        BlackboxService::append_event("lp: ina226_unavailable reason=communication_failed manufacturer=0x%04x",
                                      static_cast<unsigned>(ulp_ina226_manufacturer_id));
    }else{
        ESP_LOGI(LPTAG, "lp core i2c init success...");
    }

    if(timeout <= 0){
        ESP_LOGE(LPTAG, "lp core run timeout");
        BlackboxService::append_event("lp: run_timeout");
        return ESP_ERR_TIMEOUT;
    }else{
        current_register_raw = (int16_t*)&ulp_shunt_register_raw;
        voltage_register_raw = (uint16_t*)&ulp_voltage_register_raw;
        ESP_LOGI(LPTAG, "lp core run success...");
        ESP_LOGI(LPTAG, "first read value: voltageuV=%d currentuA=%d", ulp_voltage_uv, ulp_current_uA);
        BlackboxService::append_event("lp: run_ok voltage_uv=%ld current_ua=%ld",
                                      static_cast<long>(ulp_voltage_uv),
                                      static_cast<long>(ulp_current_uA));
    }

    xTaskCreate(print_lp_core_log_task, "print_lp_core_log", 2048, NULL, 4, NULL);
    return ESP_OK;
}
