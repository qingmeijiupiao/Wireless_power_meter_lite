#include "ulp_loader.h"
#include "ulp_lp_core.h"
#include "ulp_main.h"
#include "esp_log.h"
#include "esp_task.h"
#include "lp_core_i2c.h"
#include "ulp_app/ulp_state.h"
#include "soc/lp_clkrst_reg.h"
#include "soc/lp_clkrst_struct.h"

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

const char *LPTAG = "LP_CORE";


extern "C" {
    extern const uint8_t bin_start[] asm("_binary_ulp_main_bin_start");
    extern const uint8_t bin_end[]   asm("_binary_ulp_main_bin_end");
}

ULP_CORE_STATE& ulp_state = *(ULP_CORE_STATE*)&(ulp_ulp_state);

/*function*/

/**
 * @brief : 打印 LP 核日志附带 LP 核心看门狗
 * @return  {*}
 * @param {void*} arg
 */
void print_lp_core_log_task(void* arg){
    while (1){
        if (ulp_state.ulp_state_bits.ulp_have_log){
            ESP_LOGI(LPTAG, "lp core log: %ld", ulp_log_data);
            ulp_state.ulp_state_bits.ulp_have_log = false;
        }
        vTaskDelay(5 / portTICK_PERIOD_MS);
    }
}


esp_err_t LP_Core_Load(void){
    ESP_LOGI(LPTAG, "main core start init lp core...");
    ulp_state.ulp_state_raw = 0; // 初始化 LP 核状态

    ESP_LOGI(LPTAG, "main core start init i2c...");
    ESP_ERROR_CHECK(lp_core_i2c_master_init(LP_I2C_NUM_0, &i2c_cfg));
    ESP_LOGI(LPTAG, "lp core init i2c success...");

    // 加载 LP 核二进制文件
    ESP_ERROR_CHECK(ulp_lp_core_load_binary(bin_start, bin_end - bin_start));
    ESP_LOGI(LPTAG, "lp core load binary success...");

    LP_CLKRST.lp_clk_conf.fast_clk_sel = 1; //IDF 6.0版本默认是内部RC时钟(17.5MHz)，且没有API可以切换到外部时钟源，需要手动操作寄存器切换到外部时钟源(20MHz)

    ESP_ERROR_CHECK(ulp_lp_core_run(&lp_core_init_cfg)); 

    int32_t timeout = 200;
    while (timeout-=10){
        if(ulp_state.ulp_state_bits.ulp_run){
            break;
        }
        vTaskDelay(10/ portTICK_PERIOD_MS);
    }

    if(timeout <= 0){
        ESP_LOGE(LPTAG, "lp core run timeout");
        return ESP_ERR_TIMEOUT;
    }else{
        ESP_LOGI(LPTAG, "lp core run success...");
    }

    xTaskCreate(print_lp_core_log_task, "print_lp_core_log", 2048, NULL, 4, NULL);
    return ESP_OK;
}
