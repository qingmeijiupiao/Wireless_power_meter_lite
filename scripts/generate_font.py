#!/usr/bin/env python3
"""
从 TrueType/OpenType 字体生成包含字体数据的 C++ 头文件和源文件。
输出格式适用于嵌入式系统，假设 Font_t 结构体已在 Font.h 中定义。
此版本保留原始字符间距（步进宽度），并在生成的像素数据中包含侧边距。

用法: python generate_font.py <字体文件> <字体大小> <字体名称>
示例: python generate_font.py /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf 16 DejaVuSans
"""

import sys
import os
# 检查 PIL 库是否已安装
try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("错误：未找到 PIL 库（Pillow）。")
    print("请先安装依赖：")
    print("    pip install -r requirements.txt")
    sys.exit(1)
    
def char_advance(font, char):
    """返回字符的步进宽度（像素），四舍五入取整。"""
    try:
        return int(round(font.getlength(char)))
    except:
        return 0

def get_char_info(font, char, size):
    """
    渲染一个字符，返回其步进宽度、边界框和原始图像。
    返回：
        advance_width (int)   - 四舍五入的步进宽度（用作表中的宽度）
        bbox (tuple)          - 字符像素的边界框 (x0, y0, x1, y1)
        img (PIL.Image)       - 原始图像（大小为 (size*2, size*2)），字符绘制在 (0,0) 位置
    对于不可见字符（空格等），返回 (advance_width, None, None)。
    """
    # 创建足够大的临时图像以容纳任何字符
    temp_size = size * 2
    img = Image.new('L', (temp_size, temp_size), 0)  # 黑色背景
    draw = ImageDraw.Draw(img)

    # 在 (0,0) 处以白色绘制字符
    draw.text((0, 0), char, fill=255, font=font)

    # 获取非零像素的边界框
    bbox = img.getbbox()
    if bbox is None:
        # 不可见字符（空格等）
        w = char_advance(font, char)
        if w == 0:
            w = size // 2  # 备用宽度
        return w, None, None

    # 步进宽度
    advance = char_advance(font, char)
    # 确保步进宽度至少等于实际字符宽度，避免裁剪
    actual_width = bbox[2] - bbox[0]
    if advance < actual_width:
        advance = actual_width

    return advance, bbox, img

def generate_preview(chars, char_preview_images, output_base, font_height):
    """生成所有字符的 BMP 预览图像。"""
    cols = 16
    rows = (len(chars) + cols - 1) // cols

    max_width = max(img.width for img in char_preview_images if img is not None) if any(img is not None for img in char_preview_images) else 1
    pad_x = 2
    pad_y = 2
    cell_w = max_width + 2 * pad_x
    cell_h = font_height + 2 * pad_y

    img_w = cols * cell_w
    img_h = rows * cell_h
    preview = Image.new('L', (img_w, img_h), 255)  # 白色背景

    for idx, ch in enumerate(chars):
        row = idx // cols
        col = idx % cols
        x0 = col * cell_w
        y0 = row * cell_h

        img = char_preview_images[idx]
        if img is None:
            continue

        inverted = Image.eval(img, lambda p: 255 - p)  # 反转颜色，白底黑字更易读
        x_offset = x0 + pad_x + (max_width - img.width) // 2
        y_offset = y0 + pad_y
        preview.paste(inverted, (x_offset, y_offset))

    preview_filename = f"{output_base}_preview.bmp"
    preview.save(preview_filename)
    print(f"预览图像已保存为 {preview_filename}")

def generate_font_files(font_path, font_size, font_name):
    """生成 C++ 头文件和源文件，保留原始间距。"""
    try:
        font = ImageFont.truetype(font_path, font_size)
    except Exception as e:
        print(f"加载字体出错: {e}")
        sys.exit(1)

    # 标准可打印 ASCII 字符（32-126）
    chars = [chr(i) for i in range(32, 127)]

    # 对每个字符存储：
    #   width   = 步进宽度（像素）
    #   bbox    = 原始图像中像素的边界框 (x0,y0,x1,y1)
    #   raw_img = 原始 PIL 图像（temp_size x temp_size），字符位于 (0,0)
    widths = []
    bboxes = []
    raw_images = []
    char_heights = []   # 字符实际像素高度 (y1-y0)

    for ch in chars:
        advance, bbox, img = get_char_info(font, ch, font_size)
        widths.append(advance)
        bboxes.append(bbox)
        raw_images.append(img)
        if bbox is not None:
            char_heights.append(bbox[3] - bbox[1])
        else:
            char_heights.append(0)

    # 确定全局字体高度 = 所有字符实际高度的最大值
    font_height = max(h for h in char_heights if h > 0) if any(h > 0 for h in char_heights) else font_size

    # 构建字体数据（每个字符占用 font_height * width 字节）
    font_data = bytearray()
    char_preview_images = []  # 用于预览的最终图像（已垂直居中）

    for i, ch in enumerate(chars):
        w = widths[i]                 # 步进宽度（用于宽度表）
        bbox = bboxes[i]
        raw_img = raw_images[i]

        if bbox is None:
            # 不可见字符：填充全零，宽度为步进宽度
            data = bytearray(font_height * w)
            char_preview_images.append(None)
        else:
            # 从原始图像中提取字符区域
            x0, y0, x1, y1 = bbox
            char_w = x1 - x0
            char_h = y1 - y0

            # 裁剪出字符边界框
            char_roi = raw_img.crop((x0, y0, x1, y1))

            # 创建画布（步进宽度, 字体高度），黑色背景
            canvas = Image.new('L', (w, font_height), 0)

            # 垂直居中
            y_offset = font_height - char_h

            # 将字符 ROI 粘贴到画布 (x0, y_offset) 处
            # x0 是左侧边距（从绘图原点到字符左边缘的距离）
            # 可能为负（字符向左突出），但会被裁剪到画布内
            canvas.paste(char_roi, (x0, y_offset))

            # 获取像素数据
            data = canvas.tobytes()
            char_preview_images.append(canvas)

        font_data.extend(data)

    # 创建以字体名称命名的文件夹（如果不存在）
    output_dir = font_name
    os.makedirs(output_dir, exist_ok=True)

    # 构建输出文件路径
    header_path = os.path.join(output_dir, f"{font_name}.h")
    source_path = os.path.join(output_dir, f"{font_name}.cpp")
    preview_base = os.path.join(output_dir, font_name)  # 用于预览图像的基础名

    # 写入头文件
    guard = font_name.upper() + "_H"
    with open(header_path, 'w') as f:
        f.write(f"#ifndef {guard}\n")
        f.write(f"#define {guard}\n\n")
        f.write(f"// 字体名称 : {font_name}\n\n")
        f.write('#include "Font.h"\n\n')
        f.write(f"constexpr int {font_name}_FONT_HEIGHT = {font_height};\n\n")
        f.write(f"extern const Font_t {font_name};\n\n")
        f.write(f"#endif // {guard}\n")

    print(f"已生成头文件 {header_path}")

    # 写入源文件
    with open(source_path, 'w') as f:
        f.write(f'// {source_path}\n')
        f.write(f'#include "{font_name}.h"\n\n')
        # 字体数据数组（静态）
        f.write(f"static const uint8_t {font_name}_font_data[] = {{\n    ")
        for i, byte in enumerate(font_data):
            f.write(f'0x{byte:02x}')
            if i < len(font_data) - 1:
                f.write(', ')
            if (i + 1) % 16 == 0 and i != len(font_data) - 1:
                f.write('\n    ')
        f.write('\n};\n\n')
        # 宽度表（常量）
        f.write(f"const uint8_t {font_name}_width_table[] = {{\n    ")
        for i, w in enumerate(widths):
            f.write(f'{w:3d}')
            if i < len(widths) - 1:
                f.write(', ')
            if (i + 1) % 16 == 0 and i != len(widths) - 1:
                f.write('\n    ')
        f.write('\n};\n\n')
        # Font_t 实例
        f.write(f"const Font_t {font_name}{{\n")
        f.write(f"    .font_height = {font_name}_FONT_HEIGHT,\n")
        f.write(f"    .width_table = {font_name}_width_table,\n")
        f.write(f"    .font_data = {font_name}_font_data\n")
        f.write("};\n")

    print(f"已生成源文件 {source_path}")

    # 生成预览图像
    generate_preview(chars, char_preview_images, preview_base, font_height)

if __name__ == "__main__":
    if len(sys.argv) != 4:
        print("用法: python generate_font.py <字体文件> <字体大小> <字体名称>")
        sys.exit(1)
    font_path = sys.argv[1]
    font_size = int(sys.argv[2])
    font_name = sys.argv[3]
    generate_font_files(font_path, font_size, font_name)