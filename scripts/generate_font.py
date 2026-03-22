#!/usr/bin/env python3
"""
Generate a single C++ header file containing font data from a TrueType/OpenType font.
The output format is suitable for embedded systems with a Font_t structure (assumed to be defined in Font.h).
This version preserves the original character spacing (advance width) and includes side bearings
in the generated pixel data.

Usage: python generate_font_hpp.py <font_file> <font_size> [font_name] [output_file]
Example: python generate_font_hpp.py /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf 16 "DejaVuSans" myfont.hpp
"""

import sys
from PIL import Image, ImageDraw, ImageFont

def char_advance(font, char):
    """Return the advance width of a character in pixels (rounded)."""
    try:
        return int(round(font.getlength(char)))
    except:
        return 0

def get_char_info(font, char, size):
    """
    Render a character and return its advance width, bounding box, and the raw image.
    Returns:
        advance_width (int)   - rounded advance width (to be used as width in table)
        bbox (tuple)          - (x0, y0, x1, y1) bounding box of character pixels
        img (PIL.Image)       - the raw image (size = (size*2, size*2)) with character drawn at (0,0)
    For invisible characters (space, etc.), returns (advance_width, None, None).
    """
    # Create a temporary image large enough to hold any character
    temp_size = size * 2
    img = Image.new('L', (temp_size, temp_size), 0)  # black background
    draw = ImageDraw.Draw(img)

    # Draw the character at (0,0) with white color
    draw.text((0, 0), char, fill=255, font=font)

    # Get bounding box of non-zero pixels
    bbox = img.getbbox()
    if bbox is None:
        # Invisible character (space, etc.)
        w = char_advance(font, char)
        if w == 0:
            w = size // 2  # fallback width
        return w, None, None

    # Advance width
    advance = char_advance(font, char)
    # For consistency, ensure advance is at least the actual character width
    actual_width = bbox[2] - bbox[0]
    if advance < actual_width:
        advance = actual_width  # avoid clipping

    return advance, bbox, img

def generate_preview(chars, char_preview_images, output_prefix, font_height):
    """Generate a BMP preview image showing all characters."""
    cols = 16
    rows = (len(chars) + cols - 1) // cols

    max_width = max(img.width for img in char_preview_images if img is not None) if any(img is not None for img in char_preview_images) else 1
    pad_x = 2
    pad_y = 2
    cell_w = max_width + 2 * pad_x
    cell_h = font_height + 2 * pad_y

    img_w = cols * cell_w
    img_h = rows * cell_h
    preview = Image.new('L', (img_w, img_h), 255)  # White background

    for idx, ch in enumerate(chars):
        row = idx // cols
        col = idx % cols
        x0 = col * cell_w
        y0 = row * cell_h

        img = char_preview_images[idx]
        if img is None:
            continue

        inverted = Image.eval(img, lambda p: 255 - p)
        x_offset = x0 + pad_x + (max_width - img.width) // 2
        y_offset = y0 + pad_y
        preview.paste(inverted, (x_offset, y_offset))

    preview_filename = f"{output_prefix}_preview.bmp"
    preview.save(preview_filename)
    print(f"Preview saved as {preview_filename}")

def generate_font_hpp(font_path, font_size, font_name="Font", output_file=None):
    """Generate a single C++ header file with font data (preserving original spacing)."""
    try:
        font = ImageFont.truetype(font_path, font_size)
    except Exception as e:
        print(f"Error loading font: {e}")
        sys.exit(1)

    # Standard printable ASCII characters (32-126)
    chars = [chr(i) for i in range(32, 127)]

    # For each character, store:
    #   width   = advance width (pixels)
    #   bbox    = (x0,y0,x1,y1) bounding box of pixels in raw image
    #   raw_img = raw PIL image (temp_size x temp_size) with character at (0,0)
    widths = []
    bboxes = []
    raw_images = []
    char_heights = []   # actual pixel height of character (y1-y0)

    for ch in chars:
        advance, bbox, img = get_char_info(font, ch, font_size)
        widths.append(advance)
        bboxes.append(bbox)
        raw_images.append(img)
        if bbox is not None:
            char_heights.append(bbox[3] - bbox[1])
        else:
            char_heights.append(0)

    # Determine global font height = maximum actual character height
    font_height = max(h for h in char_heights if h > 0) if any(h > 0 for h in char_heights) else font_size

    # Build font data (each character occupies font_height * width bytes)
    font_data = bytearray()
    char_preview_images = []  # final images for preview (already centered to font_height)

    for i, ch in enumerate(chars):
        w = widths[i]                 # advance width (used in width_table)
        bbox = bboxes[i]
        raw_img = raw_images[i]

        if bbox is None:
            # Invisible char: fill zeros for the full advance width
            data = bytearray(font_height * w)
            char_preview_images.append(None)
        else:
            # Extract character region from raw image
            x0, y0, x1, y1 = bbox
            char_w = x1 - x0
            char_h = y1 - y0

            # Crop raw image to the character's bounding box
            char_roi = raw_img.crop((x0, y0, x1, y1))

            # Create canvas of size (advance_width, font_height) with black background
            canvas = Image.new('L', (w, font_height), 0)

            # Vertical centering
            y_offset = (font_height - char_h) // 2

            # Paste character ROI at (x0, y_offset)
            # x0 is the left bearing (distance from drawing origin to the left edge of character)
            # It may be negative if the character extends left of the origin, but we clip to canvas.
            canvas.paste(char_roi, (x0, y_offset))

            # Get pixel data
            data = canvas.tobytes()
            char_preview_images.append(canvas)

        font_data.extend(data)

    # Write output file
    if output_file is None:
        output_file = f"{font_name}.hpp"

    with open(output_file, 'w') as f:
        # Header guard
        guard = font_name.upper() + "_HPP"
        f.write(f"#ifndef {guard}\n")
        f.write(f"#define {guard}\n\n")

        # Font name comment
        f.write(f"//Font Name : {font_name}\n\n")

        # Include Font.h (assumed to contain Font_t definition)
        f.write('#include "Font.h"\n\n')

        # Font data array
        f.write(f"uint8_t {font_name}_font_data[] = {{\n    ")
        for i, byte in enumerate(font_data):
            f.write(f'0x{byte:02x}')
            if i < len(font_data) - 1:
                f.write(', ')
            if (i + 1) % 16 == 0 and i != len(font_data) - 1:
                f.write('\n    ')
        f.write('\n};\n\n')

        # Font_t instance
        f.write(f"const Font_t {font_name}{{\n")
        f.write(f"    .font_height = {font_height},\n")
        f.write(f"    .width_table = {{\n        ")
        for i, w in enumerate(widths):
            f.write(f'{w:3d}')
            if i < len(widths) - 1:
                f.write(', ')
            if (i + 1) % 16 == 0 and i != len(widths) - 1:
                f.write('\n        ')
        f.write('\n    },\n')
        f.write(f"    .font_data = {font_name}_font_data\n")
        f.write("};\n\n")

        f.write(f"#endif // {guard}\n")

    print(f"Generated {output_file} with font height = {font_height}")

    # Generate preview image
    generate_preview(chars, char_preview_images, font_name, font_height)

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python generate_font_hpp.py <font_file> <font_size> [font_name] [output_file]")
        sys.exit(1)
    font_path = sys.argv[1]
    font_size = int(sys.argv[2])
    font_name = sys.argv[3] if len(sys.argv) > 3 else "Font"
    output_file = sys.argv[4] if len(sys.argv) > 4 else None
    generate_font_hpp(font_path, font_size, font_name, output_file)