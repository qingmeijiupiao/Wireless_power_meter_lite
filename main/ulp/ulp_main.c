#include "ulp_lp_core.h"
#include "ulp_lp_core_gpio.h"
#include "ulp_lp_core_i2c.h"
#include "ulp_lp_core_utils.h"
#include "esp_log.h"
#define MS_TO_US(ms) ((ms) * 1000)

#define LP_VAR __attribute__((section(".rtc.bss")))
/* 定义共享变量，存放在RTC内存中（.rtc.bss段） */
volatile uint32_t ulp_counter LP_VAR;
volatile int ulp_last_gpio_state LP_VAR;
volatile bool have_log LP_VAR;
volatile uint32_t log_data LP_VAR;

#define LP_I2C_NUM I2C_NUM_0
#define INA226_I2C_ADDR 0x40

static void lp_log(uint32_t log){
    log_data = log;
    have_log = true;
}

static esp_err_t ina226_read_register(uint8_t reg, uint16_t *val)
{
    uint8_t data[2];
    esp_err_t ret = lp_core_i2c_master_write_read_device(LP_I2C_NUM, INA226_I2C_ADDR,
                                                         &reg, 1, data, 2, 10*20000);
    if(ret != ESP_OK){
        return ret;
    }
    *val = (data[0] << 8) | data[1];
    return ESP_OK;
}


int main(void){
    /* 初始化变量 */
    ulp_counter = 0;
    ulp_last_gpio_state = 0;

    /* 初始化GPIO0（LP核可访问的引脚，请查阅技术参考手册确认） */
    ulp_lp_core_gpio_init(LP_IO_NUM_0);
    uint16_t adc_value = 0;

    while (1) {
        /* 读取GPIO电平 */
        ulp_last_gpio_state = ulp_lp_core_gpio_get_level(LP_IO_NUM_0);
        ina226_read_register(0xfe, &adc_value);
        lp_log(adc_value);
        /* 计数器递增 */
        ulp_counter++;
        ulp_lp_core_delay_us(MS_TO_US(500));
    }
    return 0;
}