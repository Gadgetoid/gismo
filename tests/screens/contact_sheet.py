# /// script
# requires-python = ">=3.10"
# dependencies = ["pillow"]
# ///

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


def main():
    parser = argparse.ArgumentParser(description="Turn GISmo screen frames into PNGs and a contact sheet")
    parser.add_argument("frames", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--columns", type=int, default=6)
    args = parser.parse_args()

    images = []
    for frame in sorted(args.frames.glob("*.ppm")):
        image = Image.open(frame).convert("RGB")
        image.save(args.output / f"{frame.stem}.png")
        images.append((frame.stem.split("-", 1)[1].replace("-", " "), image))

    gap = 16
    label = 22
    width, height = images[0][1].size
    rows = -(-len(images) // args.columns)
    sheet = Image.new("RGB", (args.columns * (width + gap) + gap, rows * (height + gap + label) + gap), (40, 40, 44))
    draw = ImageDraw.Draw(sheet)
    for index, (name, image) in enumerate(images):
        x = gap + index % args.columns * (width + gap)
        y = gap + index // args.columns * (height + gap + label)
        sheet.paste(image, (x, y))
        draw.text((x, y + height + 4), name, fill=(220, 220, 220))
    sheet.save(args.output / "contact-sheet.png")
    print(f"{len(images)} screens, contact sheet {sheet.size[0]}x{sheet.size[1]}")


if __name__ == "__main__":
    main()
