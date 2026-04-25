#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import numpy as np

def generate_lut(num_points=32, max_pwm=65535, gamma=2.2, min_duty=0, brightness_max=255):
    """
    生成亮度(uint8) -> 占空比(uint16) 查找表
    返回: list of (brightness, duty)
    """
    # 亮度均匀采样 (0 ~ brightness_max)
    brightness_levels = np.linspace(0, brightness_max, num_points, dtype=np.uint16)
    brightness_float = brightness_levels.astype(np.float64) / brightness_max

    # Gamma 校正: duty = max_pwm * (亮度比例)^(1/gamma)
    duty_float = max_pwm * (brightness_float ** (1.0 / gamma))

    duty_int = np.round(duty_float).astype(np.int32)
    duty_int = np.clip(duty_int, 0, max_pwm)

    # 处理最小占空比（死区补偿）
    if min_duty > 0:
        for i in range(num_points):
            if brightness_levels[i] > 0 and duty_int[i] < min_duty:
                duty_int[i] = min_duty

    # 返回点对列表
    return [(int(brightness_levels[i]), int(duty_int[i])) for i in range(num_points)]

def write_header_file(filename, lut, gamma, min_duty):
    """写入 .h 文件，格式完全匹配用户示例"""
    with open(filename, 'w', encoding='utf-8') as f:
        f.write("// Auto-generated backlight LUT\n")
        f.write(f"// Gamma = {gamma}, min_duty = {min_duty}\n")
        f.write("// Brightness: uint8_t (0-255), Duty: uint16_t (0-65535)\n")
        f.write(f"// Table size: {len(lut)}\n\n")
        f.write("#ifndef BACKLIGHT_LUT_H\n")
        f.write("#define BACKLIGHT_LUT_H\n\n")
        f.write("#include <stdint.h>\n")
        f.write("#include <vector>\n")
        f.write("/*\n")
        f.write(" * @Description: 背光LUT表\n")
        f.write(" * @param {uint8_t} brightness 亮度,0-255 ～0-100%\n")
        f.write(" * @param {uint16_t} duty 占空比,0-65535  ～0-100%\n")
        f.write(" * @return {std::vector<std::pair<uint8_t, uint16_t>>} 背光LUT表\n")
        f.write(" * @note 亮度和占空比成线性关系,亮度增加,占空比增加\n")
        f.write("*/\n")
        f.write("const std::vector<std::pair<uint8_t, uint16_t>> backlight_lut = {\n")
        for b, d in lut:
            f.write(f"    {{ {b:3d}, {d:5d} }},\n")
        f.write("};\n\n")
        f.write("#endif // BACKLIGHT_LUT_H\n")

if __name__ == "__main__":
    # ====== 可调参数 ======
    NUM_POINTS = 32      # 表中点数
    GAMMA = 2.2          # Gamma 值
    MIN_DUTY = 200       # 最小占空比（避免死区）
    MAX_PWM = 65535      # 16位PWM满值
    # =====================

    lut = generate_lut(num_points=NUM_POINTS, max_pwm=MAX_PWM, gamma=GAMMA, min_duty=MIN_DUTY)
    write_header_file("backlight_lut.h", lut, GAMMA, MIN_DUTY)
    print(f"✅ 已生成 backlight_lut.h，共 {NUM_POINTS} 个点")
    print(f"   Gamma = {GAMMA}, min_duty = {MIN_DUTY}")