#ifndef FONT_H
#define FONT_H
#include <stdint.h>

#define FONT_CHAR_NUM 95

struct Font_t{
    const uint8_t  font_height; //只支持等高字体，高度为font_height
    uint8_t width_table[FONT_CHAR_NUM];
    uint8_t* font_data;
};

#endif