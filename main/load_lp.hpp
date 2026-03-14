#include "ulp_lp_core.h"
#include "ulp_main.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_task.h"
const char *LPTAG = "LP_CORE";
extern "C" {
    extern const uint8_t bin_start[] asm("_binary_ulp_main_bin_start");
    extern const uint8_t bin_end[]   asm("_binary_ulp_main_bin_end");
}

void LP_Core_Load(void){
    esp_err_t err;
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
    ulp_lp_core_cfg_t cfg = {
        .wakeup_source = ULP_LP_CORE_WAKEUP_SOURCE_HP_CPU,
    };
    err = ulp_lp_core_run(&cfg);
    if (err != ESP_OK) {
        while(1){
            ESP_LOGE(LPTAG, "lp core run failed: %s", esp_err_to_name(err));
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }
    ESP_LOGI(LPTAG, "lp core run success...");
}