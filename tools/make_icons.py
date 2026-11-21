# /// script
# requires-python = ">=3.10"
# dependencies = ["pillow"]
# ///

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ICONS = [
    ("my_location", 24),
    ("map", 26),
    ("layers", 24),
    ("satellite_alt", 26),
    ("checklist", 26),
    ("close", 20),
    ("directions_walk", 20),
    ("flag", 20),
    ("photo_camera", 26),
    ("star", 16),
    ("photo_library", 26),
    ("folder_open", 20),
    ("lightbulb", 26),
    ("expand_less", 24),
    ("expand_more", 24),
    ("check", 16),
    ("route", 24),
]


def read_codepoints(path):
    codepoints = {}
    for line in path.read_text().splitlines():
        name, value = line.split()
        codepoints[name] = int(value, 16)
    return codepoints


def render(font_path, codepoint, size):
    font = ImageFont.truetype(str(font_path), size)
    values = []
    for axis in font.get_variation_axes():
        name = axis["name"].lower() if isinstance(axis["name"], str) else axis["name"].decode().lower()
        if name in ("fill",):
            values.append(1)
        elif name in ("weight", "wght"):
            values.append(600)
        elif name in ("optical size", "opsz"):
            values.append(max(axis["minimum"], min(axis["maximum"], size)))
        else:
            values.append(axis["default"])
    font.set_variation_by_axes(values)
    canvas = Image.new("L", (size * 2, size * 2), 0)
    ImageDraw.Draw(canvas).text((size, size), chr(codepoint), font=font, fill=255, anchor="mm")
    box = canvas.getbbox()
    left = size - size // 2
    top = size - size // 2
    if box:
        centre_x = (box[0] + box[2]) // 2
        centre_y = (box[1] + box[3]) // 2
        left = centre_x - size // 2
        top = centre_y - size // 2
    return canvas.crop((left, top, left + size, top + size))


def main():
    parser = argparse.ArgumentParser(description="Render Material Symbols icons to alpha masks in a C++ header")
    parser.add_argument("font", type=Path, help="MaterialSymbolsRounded[FILL,GRAD,opsz,wght].ttf")
    parser.add_argument("codepoints", type=Path)
    parser.add_argument("--output", type=Path, default=Path("../src/app/icons.h"))
    parser.add_argument("--preview", type=Path)
    args = parser.parse_args()

    codepoints = read_codepoints(args.codepoints)
    lines = [
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        "namespace gismo {",
        "",
        "struct icon {",
        "    int size;",
        "    const uint8_t *alpha;",
        "};",
        "",
    ]
    previews = []
    for name, size in ICONS:
        image = render(args.font, codepoints[name], size)
        previews.append(image)
        data = image.tobytes()
        rows = [", ".join(str(value) for value in data[row * size:(row + 1) * size]) for row in range(size)]
        lines.append(f"inline constexpr uint8_t icon_{name}_alpha[{size * size}] = {{")
        lines += [f"    {row}," for row in rows]
        lines.append("};")
        lines.append(f"inline constexpr icon icon_{name} = {{{size}, icon_{name}_alpha}};")
        lines.append("")
    lines.append("}")
    args.output.write_text("\n".join(lines) + "\n")
    print(f"wrote {args.output}: {len(ICONS)} icons")

    if args.preview:
        width = sum(image.width + 4 for image in previews)
        sheet = Image.new("L", (width, max(image.height for image in previews)), 40)
        x = 0
        for image in previews:
            sheet.paste(255, (x, 0), image)
            x += image.width + 4
        sheet.resize((sheet.width * 3, sheet.height * 3), Image.Resampling.NEAREST).save(args.preview)


if __name__ == "__main__":
    main()
