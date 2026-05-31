# st7735_driver

ST7735S TFT 显示屏（0.96" 160×80）SPI 驱动，提供像素绘制、矩形填充、等高变宽字体文本渲染、图像绘制及双缓冲同步等能力。

## 模块特点

- **双缓冲**：内置两帧全屏缓冲区，绘制完成后调用 `sync_buffers()` 一次性刷屏，消除撕裂
- **RGB565 色彩**：`color_t` 类支持 RGB 三通道 / HEX 构造，自动转 RGB565 小/大端序
- **等高变宽字体**：通过 `Font_t` 结构支持不等宽字符渲染，含抗锯齿插值（`map_px_data`）
- **四方向旋转**：`Vertical / Horizontal / VerticalMirror / HorizontalMirror`
- **40 MHz SPI**：使用 `spi_device_polling_transmit` 轮询传输，低延迟

## 架构与原理

```mermaid
%%{init: { 'theme': 'base', 'themeVariables': { 'primaryColor': '#E3F2FD', 'primaryBorderColor': '#1E88E5', 'primaryTextColor': '#0D47A1', 'lineColor': '#37474F', 'clusterBkg': '#F8FBFF', 'clusterBorder': '#90CAF9' } }}%%
flowchart LR
    A["draw_pixel / fill_rect / draw_char / draw_image"] --> B["写入当前帧缓冲"]
    B --> C["sync_buffers()"]
    C --> D["set_address_window"]
    D --> E["SPI polling 传输"]:::hardware
    E --> F["ST7735S 显示"]:::hardware
    classDef hardware fill:#E8EAF6,stroke:#3F51B5,color:#1A237E;
```

## 集成与使用

```cpp
#include "st7735.h"
#include "DENGB16.h"

ST7735::Config cfg = {
    .mosi_io_num = 19, .sclk_io_num = 21, .cs_io_num = 22,
    .dc_io_num = 2, .rst_io_num = 3, .bl_io_num = 15,
    .bl_active_state = true, .host_id = SPI2_HOST
};
ST7735::init(&cfg);
ST7735::fill_screen(ST7735::BLACK);
ST7735::draw_string(0, 0, "Hello!", ST7735::WHITE, ST7735::BLACK, DENGB16);
ST7735::sync_buffers();
ST7735::set_backlight(200);
```

## API 参考

| API | 说明 |
|-----|------|
| `init(cfg, rotation)` | 初始化 SPI + 显示屏，默认横屏 |
| `draw_pixel(x, y, color)` | 绘制单像素 |
| `fill_rect(x, y, w, h, color)` | 填充矩形 |
| `fill_round_rect(x, y, w, h, radius, color, bg)` | 绘制带抗锯齿的填充圆角矩形 |
| `draw_round_rect(x, y, w, h, radius, thickness, color, bg)` | 绘制带抗锯齿的圆角矩形边框 |
| `fill_screen(color)` | 全屏填充 |
| `draw_char(x, y, c, color, bg, font)` | 绘制单字符（`const Font_t&`） |
| `draw_string(x, y, str, color, bg, font)` | 绘制字符串（`const Font_t&`） |
| `draw_image(x, y, w, h, data)` | 绘制 RGB565 图像 |
| `sync_buffers()` | 将当前缓冲区刷至屏幕，切换缓冲区 |
| `switch_buffers()` | 切换当前显示缓冲区 |
| `copy_buffers()` | 复制当前缓冲区内容到另一个缓冲区 |
| `set_rotation(rotation)` | 设置旋转方向 |
| `invert_display(invert)` | 颜色反转 |
| `set_backlight(brightness)` | 设置背光亮度（0-255） |
| `get_backlight()` | 获取当前背光亮度 |
| `get_width() / get_height()` | 获取当前分辨率 |

## 环境与依赖

- **硬件**：ST7735S TFT 显示屏（160×80），SPI 接口
- **软件**：ESP-IDF v6.0+
- **组件依赖**：`esp_driver_spi`、`esp_driver_gpio`、`freertos`、`log`
