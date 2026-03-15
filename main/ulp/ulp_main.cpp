#include "ulp_lp_core.h"
#include "ulp_lp_core_gpio.h"
#include "ulp_lp_core_utils.h"

#define MS_TO_US(ms) ((ms) * 1000)

#define LP_VAR __attribute__((section(".rtc.bss")))
/* 定义共享变量，存放在RTC内存中（.rtc.bss段） */
volatile uint32_t ulp_counter LP_VAR;
volatile int ulp_last_gpio_state LP_VAR;

int main(void){
    /* 初始化变量 */
    ulp_counter = 0;
    ulp_last_gpio_state = 0;
    
    /* 初始化GPIO0（LP核可访问的引脚，请查阅技术参考手册确认） */
    ulp_lp_core_gpio_init(LP_IO_NUM_0);

    int adc_value = 0;

    while (1) {
        /* 读取GPIO电平 */
        ulp_last_gpio_state = ulp_lp_core_gpio_get_level(LP_IO_NUM_0);

        /* 计数器递增 */
        ulp_counter++;

        ulp_lp_core_delay_us(MS_TO_US(10));
    }
    return 0;
}