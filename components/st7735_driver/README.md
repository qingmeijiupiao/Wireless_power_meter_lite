# ST7735 TFT Driver (ESP-IDF)

Driver for the **ST7735S (160x80)** display, compatible with the **Adafruit Mini TFT 0.96"**, developed for **ESP32-C6** using **ESP-IDF v5.x**.

This component is part of the full project available on GitHub:

https://github.com/T4V4RES/Driver-ST7735-ESP32-C6

---

## Features

- Support for ST7735S display (160x80)
- SPI communication using ESP-IDF
- RGB565 color format (65K colors)
- 4 screen rotations (portrait / landscape)
- Basic graphics functions:
  - pixel drawing
  - screen fill
  - rectangles
  - text rendering
- Automatic backlight management
- Tested on **ESP32-C6**

---

## Compatibility

- ESP-IDF: **v5.0 or later**
- Supported targets:
  - esp32c6

---

## Installation (ESP Component Registry)

Add the component to your ESP-IDF project:

```bash
idf.py add-dependency "t4v4res/st7735_driver"
```

```C
#include "st7735.h"

st7735_config_t cfg = {
    .mosi_io_num = 19,
    .sclk_io_num = 21,
    .cs_io_num   = 22,
    .dc_io_num   = 2,
    .rst_io_num  = 3,
    .bl_io_num   = 15,   // -1 if not used
    .host_id     = SPI2_HOST
};

st7735_init(&cfg);
st7735_fill_screen(ST7735_BLACK);
st7735_draw_string(10, 30, "Hello World!", ST7735_WHITE, ST7735_BLACK, 2);
```

##Explanation 

Full Project and Examples

This component is part of a larger project that includes:

example application

detailed documentation

wiring diagrams

explanation of the internal driver architecture

See the main repository:
https://github.com/T4V4RES/Driver-ST7735-ESP32-C6

