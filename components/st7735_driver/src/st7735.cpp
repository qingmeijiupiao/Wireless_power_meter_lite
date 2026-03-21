/**
 * @file st7735.c
 * @brief ST7735S显示屏驱动 (Adafruit Mini TFT 0.96" 160x80)
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "st7735.h"
#include "st7735_commands.h"
#include "font5x7.h"
#include <algorithm>

#define U16_LITTLE_TO_BIG(x) (((x) >> 8)&0xFF) | (((x) << 8)&0xFF)

namespace ST7735 {

static const char *TAG = "ST7735";

static constexpr uint32_t SPI_CLOCK_SPEED_HZ = 40 * 1000 * 1000;
static constexpr uint32_t MAX_TRANSFER_SIZE = 160 * 80 * 2 + 8;

static spi_device_handle_t spi = NULL;
static gpio_num_t dc_pin = GPIO_NUM_NC;
static gpio_num_t rst_pin = GPIO_NUM_NC;
static uint8_t colstart = COLSTART;
static uint8_t rowstart = ROWSTART;
static uint16_t display_width = WIDTH;
static uint16_t display_height = HEIGHT;

struct double_buffer_t{
    uint16_t data[2][WIDTH*HEIGHT];
    uint8_t current_buffer = 0;
} double_buffer;


static void write_command(uint8_t cmd) {
    spi_transaction_t t = { .length = 8, .tx_buffer = &cmd };
    gpio_set_level(dc_pin, 0);
    spi_device_polling_transmit(spi, &t);
}

static void write_data(const uint8_t *data, size_t len) {
    if (len == 0) return;
    spi_transaction_t t = { .length = len * 8, .tx_buffer = data };
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

void sync_buffers() {
    set_address_window(0, 0, display_width-1, display_height-1);
    write_data((uint8_t *)double_buffer.data[double_buffer.current_buffer], display_width*display_height*2);
    double_buffer.current_buffer = 1 - double_buffer.current_buffer;
}


esp_err_t init(const Config *cfg) {
    esp_err_t ret;
    dc_pin = static_cast<gpio_num_t>(cfg->dc_io_num);
    rst_pin = static_cast<gpio_num_t>(cfg->rst_io_num);
    
    ESP_LOGI(TAG, "ST7735 Driver - Adafruit Mini TFT 0.96");
    ESP_LOGI(TAG, "引脚: MOSI=%d CLK=%d CS=%d DC=%d RST=%d BL=%d",
             cfg->mosi_io_num, cfg->sclk_io_num, cfg->cs_io_num,
             cfg->dc_io_num, cfg->rst_io_num, cfg->bl_io_num);
    
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << dc_pin) | (1ULL << rst_pin),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf);
    
    if (cfg->bl_io_num >= 0) {
        gpio_config_t bl_conf = { .pin_bit_mask = (1ULL << cfg->bl_io_num), .mode = GPIO_MODE_OUTPUT };
        gpio_config(&bl_conf);
        gpio_set_level(static_cast<gpio_num_t>(cfg->bl_io_num), 1);
        ESP_LOGI(TAG, "Backlight enabled");
    }
    
    spi_bus_config_t buscfg = {
        .mosi_io_num = cfg->mosi_io_num, .miso_io_num = -1, .sclk_io_num = cfg->sclk_io_num,
        .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = MAX_TRANSFER_SIZE,
    };
    spi_device_interface_config_t devcfg = {
        .mode = 0,
        .clock_speed_hz = SPI_CLOCK_SPEED_HZ,
        .spics_io_num = static_cast<gpio_num_t>(cfg->cs_io_num),
        .flags = SPI_DEVICE_NO_DUMMY,
        .queue_size = 7
    };
    
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
    ESP_LOGI(TAG, "SPI @ %d MHz", SPI_CLOCK_SPEED_HZ / 1000000);
    
    gpio_set_level(rst_pin, 1); vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(rst_pin, 0); vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(rst_pin, 1); vTaskDelay(pdMS_TO_TICKS(200));
    
    write_command(ST7735_SWRESET); vTaskDelay(pdMS_TO_TICKS(150));
    write_command(ST7735_SLPOUT); vTaskDelay(pdMS_TO_TICKS(500));
    
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
    
    write_command(ST7735_MADCTL); write_data_byte(0x78);
    colstart = COLSTART; rowstart = ROWSTART; display_width = WIDTH; display_height = HEIGHT;
    
    write_command(ST7735_COLMOD); write_data_byte(0x05);
    
    write_command(ST7735_GMCTRP1);
    { uint8_t d[] = {0x02,0x1C,0x07,0x12,0x37,0x32,0x29,0x2D,0x29,0x25,0x2B,0x39,0x00,0x01,0x03,0x10}; write_data(d,16); }
    write_command(ST7735_GMCTRN1);
    { uint8_t d[] = {0x03,0x1D,0x07,0x06,0x2E,0x2C,0x29,0x2D,0x2E,0x2E,0x37,0x3F,0x00,0x00,0x02,0x10}; write_data(d,16); }
    
    write_command(ST7735_NORON); vTaskDelay(pdMS_TO_TICKS(10));
    write_command(ST7735_DISPON); vTaskDelay(pdMS_TO_TICKS(100));
    
    ESP_LOGI(TAG, "显示屏正常: %dx%d 像素", display_width, display_height);
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

void draw_pixel(uint16_t x, uint16_t y, color_t color) {
    if (x >= display_width || y >= display_height) return;
    double_buffer.data[double_buffer.current_buffer][y * display_width + x] = color.get_color_raw_big_endian();
}

void fill_screen(color_t color) {
    //ST7735::fill_rect(0, 0, display_width, display_height, color);
    std::fill(double_buffer.data[double_buffer.current_buffer], double_buffer.data[double_buffer.current_buffer]+display_width*display_height, color.get_color_raw_big_endian());
}

void set_rotation(uint8_t rotation) {
    uint8_t madctl;
    switch (rotation % 4) {
        case 0: madctl=0x08; colstart=ROWSTART; rowstart=COLSTART; display_width=WIDTH; display_height=HEIGHT; break;
        case 1: madctl=0x78; colstart=COLSTART; rowstart=ROWSTART; display_width=HEIGHT; display_height=WIDTH; break;
        case 2: madctl=0xC8; colstart=ROWSTART; rowstart=COLSTART; display_width=HEIGHT; display_height=WIDTH; break;
        case 3: madctl=0xB8; colstart=COLSTART; rowstart=ROWSTART; display_width=WIDTH; display_height=HEIGHT; break;
        default: return;
    }
    write_command(ST7735_MADCTL);
    write_data_byte(madctl);
}

void invert_display(bool invert) {
    write_command(invert ? ST7735_INVON : ST7735_INVOFF);
}

void draw_char(uint16_t x, uint16_t y, char c, color_t color, color_t bg, uint8_t size) {
    if (c < 32 || c > 127) c = '?';
    uint8_t idx = c - 32;
    for (uint8_t col = 0; col < 5; col++) {
        uint8_t line = font5x7[idx * 5 + col];
        for (uint8_t row = 0; row < 7; row++) {
            color_t px = (line & (1 << row)) ? color: bg;
            if (size == 1) ST7735::draw_pixel(x + col, y + row, px);
            else ST7735::fill_rect(x + col*size, y + row*size, size, size, px);
        }
    }
}

void draw_string(uint16_t x, uint16_t y, const char *str, color_t color, color_t bg, uint8_t size) {
    uint16_t cx = x;
    while (*str) {
        if (*str == '\n') { y += 8 * size; cx = x; }
        else { ST7735::draw_char(cx, y, *str, color, bg, size); cx += 6 * size; }
        str++;
    }
}



uint16_t get_width(void) { return display_width; }
uint16_t get_height(void) { return display_height; }

void draw_image(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *data) {
    if (x >= display_width || y >= display_height) return;
    if (x + w > display_width) w = display_width - x;
    if (y + h > display_height) h = display_height - y;
    
    for (uint16_t row = 0; row < h; row++) {
        for (uint16_t col = 0; col < w; col++) {
            double_buffer.data[double_buffer.current_buffer][(y + row) * display_width + (x + col)] = U16_LITTLE_TO_BIG(data[row * w + col]);
        }
    }
}

} // namespace ST7735