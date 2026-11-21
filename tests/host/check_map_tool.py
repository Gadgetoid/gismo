# /// script
# requires-python = ">=3.10"
# dependencies = ["numpy", "pillow"]
# ///

import math
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

import numpy

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import gmap

FNV_OFFSET = 2166136261
FNV_PRIME = 16777619


def fnv1a(hash_value, data):
    for byte in data:
        hash_value = ((hash_value ^ byte) * FNV_PRIME) & 0xFFFFFFFF
    return hash_value


def expected_hashes(reader):
    lines = []
    for number, level in enumerate(reader.levels):
        hash_value = FNV_OFFSET
        tiles = 0
        for column, row in gmap.Writer.top_down(level):
            data = reader.read(level, column, row)
            if data is None:
                continue
            count = data[1] + 1
            hash_value = fnv1a(hash_value, data[2:2 + count * 2])
            pixels = zlib.decompress(data[2 + count * 2:]) if count > 1 else bytes(gmap.TILE * gmap.TILE)
            hash_value = fnv1a(hash_value, pixels)
            tiles += 1
        lines.append(f"level {number} {tiles} {hash_value:08x}")
    return lines


def best_level(reader, metres_per_pixel):
    best, best_distance = 0, 1e9
    for number, level in enumerate(reader.levels):
        distance = abs(math.log(level.millimetres_per_pixel / 1000.0 / metres_per_pixel))
        if distance < best_distance - 1e-6:
            best, best_distance = number, distance
    return best


def reference(reader, east, north, width, height, metres_per_pixel):
    number = best_level(reader, metres_per_pixel)
    level = reader.levels[number]
    level_metres = level.millimetres_per_pixel / 1000.0
    ratio = metres_per_pixel / level_metres
    step = math.floor(ratio * 65536 + 0.5)
    left = east / level_metres - width / 2.0 * ratio
    top = north / level_metres + height / 2.0 * ratio
    left_fixed = math.floor(left * 65536 + 0.5)
    top_fixed = math.floor(top * 65536 + 0.5)
    left_pixel, left_fraction = left_fixed >> 16, left_fixed & 0xFFFF
    top_pixel, top_fraction = top_fixed >> 16, top_fixed & 0xFFFF
    xs = numpy.arange(width, dtype=numpy.int64)
    ys = numpy.arange(height, dtype=numpy.int64)
    east_pixels = left_pixel + ((left_fraction + step // 2 + xs * step) >> 16)
    north_pixels = top_pixel + ((top_fraction - ys * step - step // 2) >> 16)
    west_pixel, top_north = int(east_pixels.min()), int(north_pixels.max())
    mosaic = reader.render(level, west_pixel * level_metres, (top_north + 1) * level_metres, int(east_pixels.max()) - west_pixel + 1, top_north - int(north_pixels.min()) + 1)
    image = mosaic[(top_north - north_pixels)[:, None], (east_pixels - west_pixel)[None, :]]
    return number, numpy.where(image[..., 3] == 255, gmap.rgb565(image), 0)


def check_render(tool, pack_path, reader, east, north, width, height, metres_per_pixel):
    number, expected = reference(reader, east, north, width, height, metres_per_pixel)
    with tempfile.NamedTemporaryFile(suffix=".raw") as output:
        result = subprocess.run([tool, str(pack_path), "render", str(east), str(north), str(width), str(height), str(metres_per_pixel), output.name], check=True, capture_output=True, text=True)
        actual = numpy.fromfile(output.name, "<u2").reshape(height, width)
    same_level = int(result.stdout) == number
    differing = numpy.count_nonzero(actual != expected)
    print(f"render {east},{north} {width}x{height} at {metres_per_pixel} m/px, level {number}: {differing} differing" + ("" if same_level else f", tool chose level {result.stdout.strip()}"))
    return differing == 0 and same_level


def main():
    tool, pack_path = sys.argv[1], Path(sys.argv[2])
    reader = gmap.Reader(pack_path)

    result = subprocess.run([tool, str(pack_path), "decode"], check=True, capture_output=True, text=True)
    print(result.stderr.strip())
    actual = result.stdout.strip().splitlines()
    expected = expected_hashes(reader)
    ok = actual == expected
    print("decode: " + ("match" if ok else f"MISMATCH\n{actual}\n{expected}"))

    level = reader.levels[0]
    centre_east = (level.first_column + level.columns / 2) * level.span
    centre_north = (level.first_row + level.rows / 2) * level.span
    views = [
        (centre_east, centre_north, 240, 320, 2.0),
        (centre_east + 123.4, centre_north - 77.7, 240, 320, 1.0),
        (centre_east, centre_north, 240, 320, 0.5),
        (centre_east - 1000, centre_north + 1500, 320, 240, 4.0),
        (centre_east + 51.3, centre_north + 9.9, 240, 320, 3.0),
        (centre_east, centre_north, 240, 320, 37.5),
        (level.first_column * level.span + 10, level.first_row * level.span + 10, 240, 320, 2.0),
    ]
    for view in views:
        ok = check_render(tool, pack_path, reader, *view) and ok
    print("all passed" if ok else "FAILED")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
