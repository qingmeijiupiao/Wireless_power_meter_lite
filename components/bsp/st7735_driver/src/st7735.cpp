/**
 * @file st7735.cpp
 * @brief ST7735S显示屏驱动 (160x80)
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "st7735.h"
#include "st7735_commands.h"
#include "pwm.h"
#include "Interp.hpp"
#include "backlight_lut.h"
#include <algorithm>

namespace ST7735 {

static const char *TAG = "ST7735";

static constexpr uint32_t SPI_CLOCK_SPEED_HZ = 50 * 1000 * 1000;
static constexpr uint32_t MAX_TRANSFER_SIZE = 160 * 80 * 2 + 8;

static spi_device_handle_t spi = NULL;
static gpio_num_t dc_pin = GPIO_NUM_NC;
static gpio_num_t rst_pin = GPIO_NUM_NC;
static uint8_t colstart = COLSTART;
static uint8_t rowstart = ROWSTART;
static uint16_t display_width = WIDTH;
static uint16_t display_height = HEIGHT;

static pwm_t backlight_pwm;
static EquidistantInterp<uint8_t, uint16_t> *backlight_interp = nullptr;
static uint8_t current_brightness = 0;
static bool backlight_initialized = false;
static bool bl_active_low = false;

struct double_buffer_t{
    uint16_t data[2][WIDTH*HEIGHT];
    uint8_t current_buffer = 0;
} double_buffer;


static void write_command(uint8_t cmd) {
    spi_transaction_t t = {};
    t.length = 8;
    t.tx_buffer = &cmd;
    gpio_set_level(dc_pin, 0);
    spi_device_polling_transmit(spi, &t);
}

static void write_data(const uint8_t *data, size_t len) {
    if (len == 0) return;

    spi_transaction_t t = {};
    t.length = len * 8;
    t.tx_buffer = data;

    gpio_set_level(dc_pin, 1);
    spi_device_polling_transmit(spi, &t);
}


static inline void write_data_byte(uint8_t byte) {
    write_data(&byte, 1);
}

static void set_address_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    uint8_t data[4];
    write_command(ST7735_CASET);
    data[0] = (x0 + colstart) >> 8; data[1] = (x0 + colstart) & 0xFF;
    data[2] = (x1 + colstart) >> 8; data[3] = (x1 + colstart) & 0xFF;
    write_data(data, 4);
    write_command(ST7735_RASET);
    data[0] = (y0 + rowstart) >> 8; data[1] = (y0 + rowstart) & 0xFF;
    data[2] = (y1 + rowstart) >> 8; data[3] = (y1 + rowstart) & 0xFF;
    write_data(data, 4);
    write_command(ST7735_RAMWR);
}

void switch_buffers() {
    double_buffer.current_buffer = 1 - double_buffer.current_buffer;
}

void copy_buffers() {
    memcpy(double_buffer.data[1-double_buffer.current_buffer], double_buffer.data[double_buffer.current_buffer], display_width*display_height*2);
}



void sync_buffers() {
    set_address_window(0, 0, display_width-1, display_height-1);
    write_data((uint8_t *)double_buffer.data[double_buffer.current_buffer], display_width*display_height*2);
    switch_buffers();
}


esp_err_t init(const Config *cfg, Rotation rotation) {
    esp_err_t ret;
    dc_pin = static_cast<gpio_num_t>(cfg->dc_io_num);
    rst_pin = static_cast<gpio_num_t>(cfg->rst_io_num);
    
    ESP_LOGD(TAG, "ST7735 Driver - Adafruit Mini TFT 0.96");
    ESP_LOGD(TAG, "PINS: MOSI=%d CLK=%d CS=%d DC=%d RST=%d BL=%d",
             cfg->mosi_io_num, cfg->sclk_io_num, cfg->cs_io_num,
             cfg->dc_io_num, cfg->rst_io_num, cfg->bl_io_num);
    
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << dc_pin) | (1ULL << rst_pin);
    io_conf.mode = GPIO_MODE_OUTPUT;

    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    if (cfg->bl_io_num >= 0) {
        bl_active_low = !cfg->bl_active_state;
        backlight_interp = new EquidistantInterp<uint8_t, uint16_t>(backlight_lut);
        backlight_pwm.init(static_cast<gpio_num_t>(cfg->bl_io_num));
        backlight_initialized = true;
        set_backlight(0);
        ESP_LOGD(TAG, "Backlight PWM enabled (active %s)", bl_active_low ? "low" : "high");
    }
    
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = cfg->mosi_io_num;
    buscfg.miso_io_num = -1;
    buscfg.sclk_io_num = cfg->sclk_io_num;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = MAX_TRANSFER_SIZE;

    spi_device_interface_config_t devcfg = {};
    devcfg.mode = 0;
    devcfg.clock_speed_hz = SPI_CLOCK_SPEED_HZ;
    devcfg.spics_io_num = static_cast<gpio_num_t>(cfg->cs_io_num);
    devcfg.flags = SPI_DEVICE_NO_DUMMY;
    devcfg.queue_size = 7;
    
    ret = spi_bus_initialize(cfg->host_id, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SPI bus failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = spi_bus_add_device(cfg->host_id, &devcfg, &spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI device failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGD(TAG, "SPI @ %d MHz", SPI_CLOCK_SPEED_HZ / 1000000);
    
    /*这里的启动时序已经优化过，非必要勿动*/
    gpio_set_level(rst_pin, 1); vTaskDelay(pdMS_TO_TICKS(5));
    gpio_set_level(rst_pin, 0); vTaskDelay(pdMS_TO_TICKS(1));
    gpio_set_level(rst_pin, 1); vTaskDelay(pdMS_TO_TICKS(5));
    //write_command(ST7735_SWRESET); vTaskDelay(pdMS_TO_TICKS(120));
    write_command(ST7735_SLPOUT); vTaskDelay(pdMS_TO_TICKS(120));
    
    write_command(ST7735_FRMCTR1);
    write_data_byte(0x01); write_data_byte(0x2C); write_data_byte(0x2D);
    write_command(ST7735_FRMCTR2);
    write_data_byte(0x01); write_data_byte(0x2C); write_data_byte(0x2D);
    write_command(ST7735_FRMCTR3);
    write_data_byte(0x01); write_data_byte(0x2C); write_data_byte(0x2D);
    write_data_byte(0x01); write_data_byte(0x2C); write_data_byte(0x2D);
    
    write_command(ST7735_INVCTR); write_data_byte(0x07);
    write_command(ST7735_PWCTR1); write_data_byte(0xA2); write_data_byte(0x02); write_data_byte(0x84);
    write_command(ST7735_PWCTR2); write_data_byte(0xC5);
    write_command(ST7735_PWCTR3); write_data_byte(0x0A); write_data_byte(0x00);
    write_command(ST7735_PWCTR4); write_data_byte(0x8A); write_data_byte(0x2A);
    write_command(ST7735_PWCTR5); write_data_byte(0x8A); write_data_byte(0xEE);
    write_command(ST7735_VMCTR1); write_data_byte(0x0E);
    write_command(ST7735_INVOFF);
    
    set_rotation(rotation);

    write_command(ST7735_COLMOD); write_data_byte(0x05);
    
    write_command(ST7735_GMCTRP1);
    { uint8_t d[] = {0x02,0x1C,0x07,0x12,0x37,0x32,0x29,0x2D,0x29,0x25,0x2B,0x39,0x00,0x01,0x03,0x10}; write_data(d,16); }
    write_command(ST7735_GMCTRN1);
    { uint8_t d[] = {0x03,0x1D,0x07,0x06,0x2E,0x2C,0x29,0x2D,0x2E,0x2E,0x37,0x3F,0x00,0x00,0x02,0x10}; write_data(d,16); }
    
    write_command(ST7735_NORON);
    write_command(ST7735_DISPON);
    
    ESP_LOGD(TAG, "screen setup success: %dx%d pixels", display_width, display_height);
    return ESP_OK;
}

void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, color_t color) {
    if (x >= display_width || y >= display_height) return;
    if (x + w > display_width) w = display_width - x;
    if (y + h > display_height) h = display_height - y;
    
    for (uint16_t row = y; row < y + h; row++) {
        for (uint16_t col = x; col < x + w; col++) {
            double_buffer.data[double_buffer.current_buffer][row * display_width + col] = color.get_color_raw_big_endian();
        }
    }
}

/**
 * @brief 按透明度混合两个 RGB565 颜色。
 * @param bg 背景色 RGB565 原始值。
 * @param color 前景色 RGB565 原始值。
 * @param alpha 前景色透明度，范围 0-255。
 * @return 混合后的 RGB565 原始值。
 */
static uint16_t blend_aa_rgb565(uint16_t bg, uint16_t color, uint8_t alpha) {
    const uint16_t bg_r = (bg >> 11) & 0x1F;
    const uint16_t bg_g = (bg >> 5) & 0x3F;
    const uint16_t bg_b = bg & 0x1F;
    const uint16_t color_r = (color >> 11) & 0x1F;
    const uint16_t color_g = (color >> 5) & 0x3F;
    const uint16_t color_b = color & 0x1F;

    const uint16_t result_r = (bg_r * (255 - alpha) + color_r * alpha + 127) / 255;
    const uint16_t result_g = (bg_g * (255 - alpha) + color_g * alpha + 127) / 255;
    const uint16_t result_b = (bg_b * (255 - alpha) + color_b * alpha + 127) / 255;
    return uint16_t(result_r << 11) | uint16_t(result_g << 5) | result_b;
}

/**
 * @brief 计算像素被圆角矩形覆盖的比例。
 * @param pixel_x 相对圆角矩形左上角的 X 坐标。
 * @param pixel_y 相对圆角矩形左上角的 Y 坐标。
 * @param w 圆角矩形宽度。
 * @param h 圆角矩形高度。
 * @param radius 圆角半径。
 * @return 覆盖率，范围 0-255。
 */
static uint8_t rounded_rect_coverage(int32_t pixel_x, int32_t pixel_y,
                                     uint16_t w, uint16_t h, uint16_t radius) {
    if (w == 0 || h == 0) {
        return 0;
    }

    if (pixel_x < 0 || pixel_x >= w || pixel_y < 0 || pixel_y >= h) {
        return 0;
    }

    radius = std::min<uint16_t>(radius, std::min<uint16_t>(w / 2, h / 2));
    if (radius == 0) {
        return 255;
    }

    if ((pixel_x >= radius && pixel_x < w - radius)
        || (pixel_y >= radius && pixel_y < h - radius)) {
        return 255;
    }

    // 每轴 4 个采样点，在避免浮点计算的同时提供稳定抗锯齿效果。
    static constexpr int32_t SAMPLE_OFFSETS[] = {1, 3, 5, 7};
    static constexpr int32_t SUBPIXEL_SCALE = 8;
    const int32_t width = w * SUBPIXEL_SCALE;
    const int32_t height = h * SUBPIXEL_SCALE;
    const int32_t corner_radius = radius * SUBPIXEL_SCALE;
    const int32_t radius_squared = corner_radius * corner_radius;
    uint8_t inside_count = 0;

    for (int32_t offset_y : SAMPLE_OFFSETS) {
        const int32_t sample_y = pixel_y * SUBPIXEL_SCALE + offset_y;
        if (sample_y < 0 || sample_y >= height) {
            continue;
        }

        for (int32_t offset_x : SAMPLE_OFFSETS) {
            const int32_t sample_x = pixel_x * SUBPIXEL_SCALE + offset_x;
            if (sample_x < 0 || sample_x >= width) {
                continue;
            }

            const int32_t corner_x = sample_x < corner_radius
                ? corner_radius
                : (sample_x >= width - corner_radius ? width - corner_radius : sample_x);
            const int32_t corner_y = sample_y < corner_radius
                ? corner_radius
                : (sample_y >= height - corner_radius ? height - corner_radius : sample_y);
            const int32_t dx = sample_x - corner_x;
            const int32_t dy = sample_y - corner_y;
            if (dx * dx + dy * dy <= radius_squared) {
                inside_count++;
            }
        }
    }

    return static_cast<uint8_t>((inside_count * 255 + 8) / 16);
}

/**
 * @brief 将抗锯齿像素写入当前帧缓冲。
 * @param x 屏幕 X 坐标。
 * @param y 屏幕 Y 坐标。
 * @param alpha 前景色透明度，范围 0-255。
 * @param color 前景色。
 * @param bg 背景色。
 */
static void write_aa_pixel(uint16_t x, uint16_t y, uint8_t alpha, color_t color, color_t bg) {
    if (x >= display_width || y >= display_height) {
        return;
    }

    uint16_t px = blend_aa_rgb565(bg.get_color_raw(), color.get_color_raw(), alpha);
    double_buffer.data[double_buffer.current_buffer][y * display_width + x] = (px >> 8) | (px << 8);
}

void fill_round_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                     uint16_t radius, color_t color, color_t bg) {
    const uint32_t end_x = std::min<uint32_t>(uint32_t(x) + w, display_width);
    const uint32_t end_y = std::min<uint32_t>(uint32_t(y) + h, display_height);
    for (uint32_t screen_y = y; screen_y < end_y; screen_y++) {
        for (uint32_t screen_x = x; screen_x < end_x; screen_x++) {
            const uint8_t alpha = rounded_rect_coverage(screen_x - x, screen_y - y, w, h, radius);
            write_aa_pixel(screen_x, screen_y, alpha, color, bg);
        }
    }
}

void draw_round_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                     uint16_t radius, uint16_t thickness, color_t color, color_t bg) {
    if (thickness == 0) {
        return;
    }

    const uint32_t end_x = std::min<uint32_t>(uint32_t(x) + w, display_width);
    const uint32_t end_y = std::min<uint32_t>(uint32_t(y) + h, display_height);
    const bool has_inner_rect = uint32_t(thickness) * 2 < w && uint32_t(thickness) * 2 < h;
    const uint16_t inner_w = has_inner_rect ? w - thickness * 2 : 0;
    const uint16_t inner_h = has_inner_rect ? h - thickness * 2 : 0;
    const uint16_t inner_radius = radius > thickness ? radius - thickness : 0;

    for (uint32_t screen_y = y; screen_y < end_y; screen_y++) {
        for (uint32_t screen_x = x; screen_x < end_x; screen_x++) {
            const int32_t local_x = screen_x - x;
            const int32_t local_y = screen_y - y;
            const uint8_t outer_alpha = rounded_rect_coverage(local_x, local_y, w, h, radius);
            const uint8_t inner_alpha = rounded_rect_coverage(
                local_x - thickness, local_y - thickness, inner_w, inner_h, inner_radius);
            const uint8_t alpha = outer_alpha > inner_alpha ? outer_alpha - inner_alpha : 0;
            write_aa_pixel(screen_x, screen_y, alpha, color, bg);
        }
    }
}

void draw_pixel(uint16_t x, uint16_t y, color_t color) {
    if (x >= display_width || y >= display_height) return;
    double_buffer.data[double_buffer.current_buffer][y * display_width + x] = color.get_color_raw_big_endian();
}

void draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, color_t color) {
    // Bresenham 只使用整数运算，适合在曲线页面高频绘制短线段。
    const int16_t dx = std::abs(x1 - x0);
    const int16_t sx = x0 < x1 ? 1 : -1;
    const int16_t dy = -std::abs(y1 - y0);
    const int16_t sy = y0 < y1 ? 1 : -1;
    int16_t error = dx + dy;

    while (true) {
        if (x0 >= 0 && y0 >= 0) {
            draw_pixel(static_cast<uint16_t>(x0), static_cast<uint16_t>(y0), color);
        }
        if (x0 == x1 && y0 == y1) {
            break;
        }

        const int16_t doubled_error = error * 2;
        if (doubled_error >= dy) {
            error += dy;
            x0 += sx;
        }
        if (doubled_error <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

void fill_screen(color_t color) {
    std::fill(double_buffer.data[double_buffer.current_buffer], double_buffer.data[double_buffer.current_buffer]+display_width*display_height, color.get_color_raw_big_endian());
}

void set_rotation(Rotation rotation) {
    uint8_t madctl;
    switch (rotation) {
        case Rotation::Vertical: madctl=0x08; colstart=ROWSTART; rowstart=COLSTART; display_width=HEIGHT; display_height=WIDTH; break;
        case Rotation::Horizontal: madctl=0x78; colstart=COLSTART; rowstart=ROWSTART; display_width=WIDTH; display_height=HEIGHT; break;
        case Rotation::VerticalMirror: madctl=0xC8; colstart=ROWSTART; rowstart=COLSTART; display_width=HEIGHT; display_height=WIDTH; break;
        case Rotation::HorizontalMirror: madctl=0xB8; colstart=COLSTART; rowstart=ROWSTART; display_width=WIDTH; display_height=HEIGHT; break;
        default: return;
    }
    write_command(ST7735_MADCTL);
    write_data_byte(madctl);
}

void invert_display(bool invert) {
    write_command(invert ? ST7735_INVON : ST7735_INVOFF);
}

static uint16_t map_px_data(uint8_t px_val,uint16_t bg,uint16_t color){
    // 将RGB565颜色值分解为R、G、B分量
    uint8_t bg_r = (bg >> 11) & 0x1F;  // 5位红色
    uint8_t bg_g = (bg >> 5) & 0x3F;   // 6位绿色
    uint8_t bg_b = bg & 0x1F;          // 5位蓝色
    
    uint8_t color_r = (color >> 11) & 0x1F;
    uint8_t color_g = (color >> 5) & 0x3F;
    uint8_t color_b = color & 0x1F;
    
    // 分别对R、G、B分量进行插值
    uint8_t result_r = (px_val * (color_r - bg_r) / 255) + bg_r;
    uint8_t result_g = (px_val * (color_g - bg_g) / 255) + bg_g;
    uint8_t result_b = (px_val * (color_b - bg_b) / 255) + bg_b;
    
    // 重新组合成RGB565格式
    return uint16_t(result_r << 11) | uint16_t(result_g << 5) | uint16_t(result_b);
}

static uint32_t get_char_start_index(char c,const Font_t& font){
    uint8_t index = c - ' ';
    uint32_t start_index = 0;
    for (int i  = 0; i < index; i++){
        start_index+=font.font_height*font.width_table[i];
    }
    return start_index;
}


void draw_char(uint16_t x, uint16_t y, char c, color_t color, color_t bg, const Font_t& font) {
    if (c < 32 || c > 127) c = '?';
    uint8_t idx = c - 32;
    uint32_t start_index = get_char_start_index(c,font);
    uint32_t font_px_index = start_index;

    for (uint32_t line = 0;line<font.font_height;line++){
        for (uint8_t col = 0; col < font.width_table[idx]; col++) {
            uint8_t font_val=font.font_data[font_px_index];
            uint16_t px = map_px_data(font_val,bg.get_color_raw(),color.get_color_raw());
            px = (px>>8) | (px<<8);
            double_buffer.data[double_buffer.current_buffer][(y + line) * display_width + x + col] = px;
            font_px_index++;
        }
    }
}

void draw_string(uint16_t x, uint16_t y, const char *str, color_t color, color_t bg,const Font_t& font) {
    uint16_t cx = x;
    while (*str) {
        if (*str == '\n') { y += font.font_height; cx = x; }
        else { ST7735::draw_char(cx, y, *str, color, bg, font); cx += font.width_table[*str-32]; }
        str++;
    }
}



uint16_t get_width(void) { return display_width; }
uint16_t get_height(void) { return display_height; }

esp_err_t set_backlight(uint8_t brightness) {
    if (!backlight_initialized) {
        ESP_LOGE("ST7735", "backlight not supported");
        return ESP_ERR_NOT_SUPPORTED;
    }

    if(brightness == current_brightness){
        return ESP_OK;
    }

    current_brightness = brightness;
    uint16_t duty = backlight_interp->interpolate(brightness);
    float percent = (float)duty / 65535.0f * 100.0f;
    if (bl_active_low) {
        percent = 100.0f - percent;
    }
    return backlight_pwm.set_duty_percent(percent);
}

uint8_t get_backlight() {
    return current_brightness;
}

void draw_image(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *data) {
    if (x >= display_width || y >= display_height) return;
    if (x + w > display_width) w = display_width - x;
    if (y + h > display_height) h = display_height - y;
    uint16_t px;
    for (uint16_t row = 0; row < h; row++) {
        for (uint16_t col = 0; col < w; col++) {
            px = data[row * w + col];
            double_buffer.data[double_buffer.current_buffer]
            [(y + row) * display_width + (x + col)] = (px>>8) | (px<<8);
        }
    }
}

} // namespace ST7735
