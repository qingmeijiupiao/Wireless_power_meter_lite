#pragma once

// ST7735命令定义
#define ST7735_NOP       0x00
#define ST7735_SWRESET   0x01  // 软件复位
#define ST7735_RDDID     0x04  // 读取显示ID
#define ST7735_RDDST     0x09  // 读取显示状态

#define ST7735_SLPIN     0x10  // 进入睡眠模式
#define ST7735_SLPOUT    0x11  // 退出睡眠模式
#define ST7735_PTLON     0x12  // 部分模式开启
#define ST7735_NORON     0x13  // 正常显示模式开启

#define ST7735_INVOFF    0x20  // 显示反转关闭
#define ST7735_INVON     0x21  // 显示反转开启
#define ST7735_GAMSET    0x26  // Gamma设置
#define ST7735_DISPOFF   0x28  // 显示关闭
#define ST7735_DISPON    0x29  // 显示开启

#define ST7735_CASET     0x2A  // 列地址设置
#define ST7735_RASET     0x2B  // 行地址设置
#define ST7735_RAMWR     0x2C  // 内存写入
#define ST7735_RAMRD     0x2E  // 内存读取

#define ST7735_PTLAR     0x30  // 部分区域
#define ST7735_TEOFF     0x34  // 撕裂效应线关闭
#define ST7735_TEON      0x35  // 撕裂效应线开启
#define ST7735_MADCTL    0x36  // 内存数据访问控制
#define ST7735_IDMOFF    0x38  // 空闲模式关闭
#define ST7735_IDMON     0x39  // 空闲模式开启
#define ST7735_COLMOD    0x3A  // 接口像素格式

#define ST7735_RDID1     0xDA  // 读取ID1
#define ST7735_RDID2     0xDB  // 读取ID2
#define ST7735_RDID3     0xDC  // 读取ID3

// 帧率控制
#define ST7735_FRMCTR1   0xB1  // 帧率控制（正常模式）
#define ST7735_FRMCTR2   0xB2  // 帧率控制（空闲模式）
#define ST7735_FRMCTR3   0xB3  // 帧率控制（部分模式）

// 电源控制
#define ST7735_INVCTR    0xB4  // 显示反转控制
#define ST7735_PWCTR1    0xC0  // 电源控制1
#define ST7735_PWCTR2    0xC1  // 电源控制2
#define ST7735_PWCTR3    0xC2  // 电源控制3
#define ST7735_PWCTR4    0xC3  // 电源控制4
#define ST7735_PWCTR5    0xC4  // 电源控制5
#define ST7735_VMCTR1    0xC5  // VCOM控制1

// Gamma
#define ST7735_GMCTRP1   0xE0  // Gamma '+'极性校正
#define ST7735_GMCTRN1   0xE1  // Gamma '-'极性校正

// MADCTL位
#define ST7735_MADCTL_MY  0x80  // 行地址顺序
#define ST7735_MADCTL_MX  0x40  // 列地址顺序
#define ST7735_MADCTL_MV  0x20  // 行/列交换
#define ST7735_MADCTL_ML  0x10  // 垂直刷新顺序
#define ST7735_MADCTL_RGB 0x00  // RGB顺序
#define ST7735_MADCTL_BGR 0x08  // BGR顺序
#define ST7735_MADCTL_MH  0x04  // 水平刷新顺序