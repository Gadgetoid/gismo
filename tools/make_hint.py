# /// script
# requires-python = ">=3.10"
# dependencies = ["pillow"]
# ///

import argparse
import struct
from pathlib import Path

from PIL import Image, ImageOps

WIDTH = 320
HEIGHT = 240


def to_bmp565(image):
    pixels = image.convert("RGB").load()
    row_bytes = (WIDTH * 2 + 3) & ~3
    header = struct.pack("<2sIIIIiiHHIIiiII3I", b"BM", 66 + row_bytes * HEIGHT, 0, 66, 40, WIDTH, HEIGHT, 1, 16, 3, row_bytes * HEIGHT, 2835, 2835, 0, 0, 0xF800, 0x07E0, 0x001F)
    rows = []
    for y in reversed(range(HEIGHT)):
        row = bytearray()
        for x in range(WIDTH):
            red, green, blue = pixels[x, y]
            row += struct.pack("<H", (red >> 3) << 11 | (green >> 2) << 5 | blue >> 3)
        row += bytes(row_bytes - len(row))
        rows.append(bytes(row))
    return header + b"".join(rows)


def main():
    parser = argparse.ArgumentParser(description="Turn photos into GISmo photo hints: 320x240 RGB565 BMPs, centre-cropped to 4:3")
    parser.add_argument("images", nargs="+", type=Path)
    parser.add_argument("--output", type=Path, required=True, help="folder for the .bmp hints (the Missions folder)")
    args = parser.parse_args()

    args.output.mkdir(parents=True, exist_ok=True)
    for path in args.images:
        image = ImageOps.exif_transpose(Image.open(path))
        image = ImageOps.fit(image.convert("RGB"), (WIDTH, HEIGHT), Image.Resampling.LANCZOS)
        target = args.output / f"{path.stem}.bmp"
        target.write_bytes(to_bmp565(image))
        print(f"{path} -> {target}")


if __name__ == "__main__":
    main()
