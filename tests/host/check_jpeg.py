# /// script
# requires-python = ">=3.10"
# dependencies = ["numpy", "pillow"]
# ///

import io
import math
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import gmap

WIDTH, HEIGHT = 320, 240


def source_image(pack_path):
    reader = gmap.Reader(pack_path)
    level = reader.levels[1]
    west = (level.first_column + 1) * level.span
    north = (level.first_row + level.rows - 1) * level.span
    rgba = reader.render(level, west, north, WIDTH, HEIGHT)
    xs = numpy.linspace(0, 255, WIDTH)[None, :]
    ys = numpy.linspace(0, 255, HEIGHT)[:, None]
    gradient = numpy.stack([xs + 0 * ys, ys + 0 * xs, 255 - (xs + ys) / 2], axis=-1).astype(numpy.uint8)
    rgb = rgba[..., :3].copy()
    rgb[HEIGHT // 2:, : WIDTH // 2] = gradient[HEIGHT // 2:, : WIDTH // 2]
    return gmap.rgb565(rgb)


def psnr(decoded, expected):
    return 10 * math.log10(255 ** 2 / ((decoded - expected) ** 2).mean())


def reference_psnr(expected, quality):
    buffer = io.BytesIO()
    Image.fromarray(expected.astype(numpy.uint8), "RGB").save(buffer, "JPEG", quality=quality, subsampling=2)
    return psnr(numpy.asarray(Image.open(buffer).convert("RGB")).astype(numpy.float64), expected)


def to_degrees(values, reference):
    degrees = float(values[0]) + float(values[1]) / 60 + float(values[2]) / 3600
    return -degrees if reference in ("S", "W") else degrees


def main():
    tool, pack_path = sys.argv[1], Path(sys.argv[2])
    packed = source_image(pack_path)
    expected = gmap.rgb_from_565(packed).astype(numpy.float64)
    ok = True
    with tempfile.TemporaryDirectory() as folder:
        raw = Path(folder) / "input.raw"
        packed.astype("<u2").tofile(raw)
        for quality in (60, 85, 95):
            output = Path(folder) / f"q{quality}.jpg"
            result = subprocess.run([tool, str(raw), str(WIDTH), str(HEIGHT), str(quality), str(output)], check=True, capture_output=True, text=True)
            image = Image.open(output)
            image.load()
            decoded = numpy.asarray(image.convert("RGB")).astype(numpy.float64)
            ours = psnr(decoded, expected)
            minimum = reference_psnr(expected, quality) - 0.5
            print(f"quality {quality}: {result.stdout.strip()}, PSNR {ours:.1f} dB (want >= {minimum:.1f}, Pillow 4:2:0 less 0.5)")
            ok = ok and ours >= minimum and image.size == (WIDTH, HEIGHT)

            exif = image.getexif()
            gps = exif.get_ifd(0x8825)
            latitude = to_degrees(gps[2], gps[1])
            longitude = to_degrees(gps[4], gps[3])
            altitude = float(gps[6]) * (-1 if gps.get(5, 0) in (1, b"\x01") else 1)
            fields_ok = (
                exif.get(0x010F) == "GISmo"
                and exif.get(0x0110) == "Trimble Juno SB"
                and exif.get(0x0132) == "2026:11:14 12:42:15"
                and abs(latitude - 52.2525051) < 1e-6
                and abs(longitude - 0.0184061) < 1e-6
                and abs(altitude - 21.4) < 0.05
                and gps.get(29) == "2026:11:14"
                and [float(value) for value in gps[7]] == [12.0, 42.0, 15.5]
                and tuple(gps[0]) == (2, 3, 0, 0)
            )
            if quality == 85:
                print(f"exif: {exif.get(0x010F)}, {exif.get(0x0110)}, {exif.get(0x0132)}; gps {latitude:.7f}, {longitude:.7f}, {altitude:.1f} m, {gps.get(29)} {[float(v) for v in gps[7]]}")
            ok = ok and fields_ok
    print("all passed" if ok else "FAILED")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
