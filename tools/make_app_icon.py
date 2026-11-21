# /// script
# requires-python = ">=3.10"
# dependencies = ["pillow"]
# ///

import argparse
import struct
from pathlib import Path

from PIL import Image, ImageDraw

from make_icons import read_codepoints, render

SIZES = (16, 32)
SUPERSAMPLE = 8
BACKGROUND_TOP = (255, 170, 30)
BACKGROUND_BOTTOM = (235, 100, 0)


def draw_icon(font, codepoint, size):
    large = size * SUPERSAMPLE
    background = Image.new("RGBA", (large, large), (0, 0, 0, 0))
    gradient = Image.new("RGBA", (large, large))
    for y in range(large):
        mix = y / (large - 1)
        colour = tuple(round(top + (bottom - top) * mix) for top, bottom in zip(BACKGROUND_TOP, BACKGROUND_BOTTOM))
        ImageDraw.Draw(gradient).line([(0, y), (large, y)], fill=colour + (255,))
    mask = Image.new("L", (large, large), 0)
    inset = large // 32
    ImageDraw.Draw(mask).rounded_rectangle([inset, inset, large - 1 - inset, large - 1 - inset], radius=large // 5, fill=255)
    background.paste(gradient, (0, 0), mask)
    image = background.resize((size, size), Image.Resampling.LANCZOS)

    glyph_size = round(size * 0.8)
    glyph = render(font, codepoint, glyph_size * 4).resize((glyph_size, glyph_size), Image.Resampling.LANCZOS)
    white = Image.new("RGBA", (glyph_size, glyph_size), (255, 255, 255, 255))
    offset = (size - glyph_size) // 2
    image.paste(white, (offset, offset), glyph)
    return image


def bitmap_entry(image):
    width, height = image.size
    header = struct.pack("<IiiHHIIiiII", 40, width, height * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    pixels = bytearray()
    for y in reversed(range(height)):
        for x in range(width):
            red, green, blue, alpha = image.getpixel((x, y))
            pixels += bytes((blue, green, red, alpha))
    mask_stride = ((width + 31) // 32) * 4
    mask = bytearray()
    for y in reversed(range(height)):
        row = bytearray(mask_stride)
        for x in range(width):
            if image.getpixel((x, y))[3] < 128:
                row[x // 8] |= 0x80 >> (x % 8)
        mask += row
    return header + bytes(pixels) + bytes(mask)


def main():
    parser = argparse.ArgumentParser(description="Write GISmo's .ico with 32-bit bitmap entries Windows CE can read")
    parser.add_argument("font", type=Path)
    parser.add_argument("codepoints", type=Path)
    parser.add_argument("--glyph", default="explore")
    parser.add_argument("--output", type=Path, default=Path("../assets/gismo.ico"))
    parser.add_argument("--preview", type=Path)
    args = parser.parse_args()

    codepoint = read_codepoints(args.codepoints)[args.glyph]
    images = [draw_icon(args.font, codepoint, size) for size in SIZES]
    entries = [bitmap_entry(image) for image in images]
    directory = struct.pack("<HHH", 0, 1, len(entries))
    offset = 6 + 16 * len(entries)
    for image, entry in zip(images, entries):
        directory += struct.pack("<BBBBHHII", image.width, image.height, 0, 0, 1, 32, len(entry), offset)
        offset += len(entry)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(directory + b"".join(entries))
    print(f"wrote {args.output}")

    if args.preview:
        sheet = Image.new("RGBA", (sum(image.width * 4 + 8 for image in images), max(SIZES) * 4), (60, 60, 60, 255))
        x = 0
        for image in images:
            sheet.alpha_composite(image.resize((image.width * 4, image.height * 4), Image.Resampling.NEAREST), (x, 0))
            x += image.width * 4 + 8
        sheet.save(args.preview)


if __name__ == "__main__":
    main()
