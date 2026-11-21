# /// script
# requires-python = ">=3.10"
# dependencies = ["numpy", "pillow", "pyproj"]
# ///

import argparse
from pathlib import Path

import numpy
from PIL import Image

import gmap
from sources import read_map_file, sheet_pixels


def main():
    parser = argparse.ArgumentParser(description="Compare a GISmo map pack's finest level with its source sheets")
    parser.add_argument("pack", type=Path)
    parser.add_argument("folder", type=Path)
    parser.add_argument("--bbox", type=float, nargs=4, metavar=("WEST", "SOUTH", "EAST", "NORTH"))
    parser.add_argument("--render", type=float, nargs=4, metavar=("WEST", "NORTH", "WIDTH", "HEIGHT"), help="also write a PNG of this area (metres)")
    parser.add_argument("--level", type=int, default=0)
    parser.add_argument("--png", type=Path, default=Path("render.png"))
    args = parser.parse_args()

    reader = gmap.Reader(args.pack)
    level = reader.levels[0]
    total = mismatched = 0
    for path in sorted(args.folder.glob("*.map")):
        sheet = read_map_file(path)
        if sheet.millimetres_per_pixel != level.millimetres_per_pixel:
            continue
        if args.bbox and (sheet.east <= args.bbox[0] or sheet.west >= args.bbox[2] or sheet.north <= args.bbox[1] or sheet.south >= args.bbox[3]):
            continue
        indices, palette = sheet_pixels(sheet.key)
        expected = gmap.rgb_from_565(gmap.rgb565(palette[indices]))
        actual = reader.render(level, sheet.west, sheet.north, sheet.width, sheet.height)
        covered = actual[..., 3] == 255
        if args.bbox:
            metres_per_pixel = level.metres_per_pixel
            xs = sheet.west + (numpy.arange(sheet.width) + 0.5) * metres_per_pixel
            ys = sheet.north - (numpy.arange(sheet.height) + 0.5) * metres_per_pixel
            inside = ((xs >= args.bbox[0]) & (xs < args.bbox[2]))[None, :] & ((ys >= args.bbox[1]) & (ys < args.bbox[3]))[:, None]
        else:
            inside = numpy.ones(covered.shape, bool)
        uncovered = numpy.count_nonzero(inside & ~covered)
        differing = numpy.count_nonzero(inside & covered & (actual[..., :3] != expected).any(axis=-1))
        total += numpy.count_nonzero(inside)
        mismatched += differing
        print(f"{path.stem}: {differing} differing, {uncovered} uncovered of {numpy.count_nonzero(inside)}")
    print(f"total: {mismatched} of {total} pixels differ ({100 * mismatched / max(total, 1):.3f}%)")

    if args.render:
        west, north, width, height = args.render
        chosen = reader.levels[args.level]
        image = reader.render(chosen, west, north, round(width / chosen.metres_per_pixel), round(height / chosen.metres_per_pixel))
        Image.fromarray(image, "RGBA").save(args.png)
        print(f"wrote {args.png}")


if __name__ == "__main__":
    main()
