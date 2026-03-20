/**
 * @file st7735.h
 * @brief ST7735S显示屏驱动
 * 
 * 通过SPI控制TFT ST7735S显示屏的公共API。
 * 包含绘制像素、矩形、文本等函数。
 * 
 * @example
 * ```c
 * st7735_config_t cfg = {
 *     .mosi_io_num = 19, .sclk_io_num = 21, .cs_io_num = 22,
 *     .dc_io_num = 2, .rst_io_num = 3, .bl_io_num = 15,
 *     .host_id = SPI2_HOST
 * };
 * st7735_init(&cfg);
 * st7735_fill_screen(ST7735_BLACK);
 * st7735_draw_string(10, 10, "Hello!", ST7735_WHITE, ST7735_BLACK, 2);
 * ```
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
namespace ST7735 {
#endif

/* ==================== 显示屏配置 ==================== */

/** 显示屏在横向模式下的宽度（像素） */
static constexpr uint16_t WIDTH = 160;

/** 显示屏在横向模式下的高度（像素） */
static constexpr uint16_t HEIGHT = 80;

/* ==================== RGB565颜色 ==================== */

static constexpr uint16_t BLACK    = 0x0000;  /**< 黑色 */
static constexpr uint16_t WHITE    = 0xFFFF;  /**< 白色 */
static constexpr uint16_t RED      = 0xF800;  /**< 红色 */
static constexpr uint16_t GREEN    = 0x07E0;  /**< 绿色 */
static constexpr uint16_t BLUE     = 0x001F;  /**< 蓝色 */
static constexpr uint16_t CYAN     = 0x07FF;  /**< 青色 */
static constexpr uint16_t MAGENTA  = 0xF81F;  /**< 洋红色 */
static constexpr uint16_t YELLOW   = 0xFFE0;  /**< 黄色 */
static constexpr uint16_t ORANGE   = 0xFC00;  /**< 橙色 */
static constexpr uint16_t GRAY     = 0x7BEF;  /**< 灰色 */

/** 从RGB分量（0-255）创建RGB565颜色的宏 */
static constexpr uint16_t RGB565(uint8_t r, uint8_t g, uint8_t b) {
    return (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/* ==================== 结构体 ==================== */
static constexpr uint8_t COLSTART = 0;
static constexpr uint8_t ROWSTART = 24;

/**
 * @brief 显示屏硬件配置
 */
struct Config {
    int mosi_io_num;           /**< MOSI的GPIO引脚（显示屏上的SI） */
    int sclk_io_num;           /**< 时钟的GPIO引脚（SCK） */
    int cs_io_num;             /**< 片选的GPIO引脚（TCS） */
    int dc_io_num;             /**< 数据/命令的GPIO引脚（DC） */
    int rst_io_num;            /**< 复位的GPIO引脚（RST） */
    int bl_io_num;             /**< 背光的GPIO引脚（Lite），-1表示未使用 */
    spi_host_device_t host_id; /**< SPI主机（SPI2_HOST或SPI3_HOST） */
};

/* ==================== 公共函数 ==================== */

/**
 * @brief 初始化ST7735显示屏
 * @param cfg 指向配置结构的指针
 * @return 成功返回ESP_OK，否则返回错误代码
 */
esp_err_t init(const Config *cfg);

/**
 * @brief 绘制一个像素
 * @param x X坐标（0到width-1）
 * @param y Y坐标（0到height-1）
 * @param color RGB565格式的颜色
 */
void draw_pixel(uint16_t x, uint16_t y, uint16_t color);

/**
 * @brief 用颜色填充矩形
 * @param x 左上角的X坐标
 * @param y 左上角的Y坐标
 * @param w 矩形的宽度
 * @param h 矩形的高度
 * @param color RGB565格式的颜色
 */
void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

/**
 * @brief 用颜色填充整个屏幕
 * @param color RGB565格式的颜色
 */
void fill_screen(uint16_t color);

/**
 * @brief 设置显示屏的旋转方向
 * @param rotation 0到3的值：
 *        - 0: 纵向（80x160）
 *        - 1: 横向（160x80）[默认]
 *        - 2: 反向纵向（80x160）
 *        - 3: 反向横向（160x80）
 */
void set_rotation(uint8_t rotation);

/**
 * @brief 反转显示屏的颜色
 * @param invert true表示反转，false表示正常
 */
void invert_display(bool invert);

/**
 * @brief 绘制一个字符
 * @param x X坐标
 * @param y Y坐标
 * @param c ASCII字符（32-127）
 * @param color 文本颜色
 * @param bg 背景颜色
 * @param size 缩放比例（1 = 5x7, 2 = 10x14, 等）
 */
void draw_char(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg, uint8_t size);

/**
 * @brief 绘制文本字符串
 * @param x 起始X坐标
 * @param y 起始Y坐标
 * @param str 以NULL结尾的字符串
 * @param color 文本颜色
 * @param bg 背景颜色
 * @param size 文本缩放比例
 */
void draw_string(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg, uint8_t size);

/**
 * @brief 获取当前显示屏的宽度
 * @return 宽度（像素）（取决于旋转方向）
 */
uint16_t get_width(void);

/**
 * @brief 获取当前显示屏的高度
 * @return 高度（像素）（取决于旋转方向）
 */
uint16_t get_height(void);

/**
 * @brief 在显示屏上绘制RGB565图像
 * @param x 左上角的X坐标
 * @param y 左上角的Y坐标
 * @param w 图像的宽度
 * @param h 图像的高度
 * @param data 指向RGB565像素数组的指针（大端序）
 */
void draw_image(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *data);

/**
 * @brief 同步双缓冲区，将当前缓冲区的内容发送到显示屏
 */
void sync_buffers();
#ifdef __cplusplus
} // namespace ST7735
#endif