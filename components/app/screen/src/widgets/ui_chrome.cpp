/*
 * @version: 1.0
 * @LastEditors: qingmeijiupiao
 * @Description: 页面级公共装饰绘制实现
 * @Author: qingmeijiupiao
 * @LastEditTime: 2026-06-24
 */
#include "widgets/ui_chrome.h"

#include "st7735.h"

namespace SCREEN {

void draw_edit_indicator() {
    ST7735::fill_rect(0, 0, ST7735::WIDTH, 1, ST7735::YELLOW);
}

} // namespace SCREEN
