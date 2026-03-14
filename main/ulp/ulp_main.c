#define MS_TO_US(ms) ((ms) * 1000)
#include "ulp_lp_core.h"
#include "ulp_lp_core_gpio.h"
#include "ulp_lp_core_utils.h"
#define LP_VAR __attribute__((section(".rtc.bss")))
/* 定义共享变量，存放在RTC内存中（.rtc.bss段） */
volatile uint32_t ulp_counter LP_VAR;
volatile int ulp_last_gpio_state LP_VAR;
int main(void){
    /* 初始化变量 */
    ulp_counter = 0;
    ulp_last_gpio_state = 0;

    /* 初始化GPIO0（LP核可访问的引脚，请查阅技术参考手册确认） */
    ulp_lp_core_gpio_init(GPIO_NUM_0);

    while (1) {
        /* 读取GPIO电平 */
        ulp_last_gpio_state = ulp_lp_core_gpio_get_level(GPIO_NUM_0);

        /* 计数器递增 */
        ulp_counter++;

        // /* 简单延时，避免计数过快 */
        // for (int i = 0; i < 10000; i++) {
        //     asm volatile("nop");
        // }
        ulp_lp_core_delay_us(MS_TO_US(1));
    }
    return 0;
}