#include "ulp_lp_core.h"
#include "ulp_main.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_task.h"
#include "lp_core_i2c.h"

const char *LPTAG = "LP_CORE";
extern "C" {
    extern const uint8_t bin_start[] asm("_binary_ulp_main_bin_start");
    extern const uint8_t bin_end[]   asm("_binary_ulp_main_bin_end");
}

void print_lp_core_log(void* arg){
    while (1){
        if(ulp_have_log){
            ESP_LOGI(LPTAG, "ulp log: 0x%lx", ulp_log_data);
            ulp_have_log = false;
            ulp_log_data = 0;
        }
        vTaskDelay(5 / portTICK_PERIOD_MS);
    }
}

void LP_Core_Load(void){
    esp_err_t err;
    // 初始化 I2C
    ESP_LOGI(LPTAG, "main core start init i2c...");
    lp_core_i2c_cfg_t i2c_cfg;
    i2c_cfg.i2c_pin_cfg.sda_io_num = gpio_num_t::GPIO_NUM_6;
    i2c_cfg.i2c_pin_cfg.scl_io_num = gpio_num_t::GPIO_NUM_7;
    i2c_cfg.i2c_pin_cfg.sda_pullup_en = true;
    i2c_cfg.i2c_pin_cfg.scl_pullup_en = true;
    i2c_cfg.i2c_timing_cfg.clk_speed_hz = 100000;
    i2c_cfg.i2c_src_clk = LP_I2C_SCLK_DEFAULT;

    err = lp_core_i2c_master_init(LP_I2C_NUM_0, (const lp_core_i2c_cfg_t*)&i2c_cfg);
    if (err != ESP_OK) {
        while(1){
            ESP_LOGE(LPTAG, "lp core init i2c failed: %s", esp_err_to_name(err));
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }
    ESP_LOGI(LPTAG, "lp core init i2c success...");
    
    ESP_LOGI(LPTAG, "main core start load lp core...");
    // 加载 LP 核二进制文件
    err = ulp_lp_core_load_binary(bin_start, bin_end - bin_start);
    if (err != ESP_OK) {
        while(1){
            ESP_LOGE(LPTAG, "lp core load binary failed: %s", esp_err_to_name(err));
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }
    ESP_LOGI(LPTAG, "lp core load binary success...");
    // 配置 LP 核运行参数
    ulp_lp_core_cfg_t cfg;
    cfg.wakeup_source = ULP_LP_CORE_WAKEUP_SOURCE_HP_CPU;
    err = ulp_lp_core_run(&cfg);
    if (err != ESP_OK) {
        while(1){
            ESP_LOGE(LPTAG, "lp core run failed: %s", esp_err_to_name(err));
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }

    xTaskCreate(print_lp_core_log, "print_lp_core_log", 2048, NULL, 4, NULL);
    ESP_LOGI(LPTAG, "lp core run success...");
}