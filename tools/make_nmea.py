# /// script
# requires-python = ">=3.10"
# dependencies = ["pyproj"]
# ///

import argparse
import datetime
import math
import random
from functools import reduce
from pathlib import Path

from pyproj import Transformer

bng_to_wgs84 = Transformer.from_crs("EPSG:27700", "EPSG:4326", always_xy=True)

SATELLITES = [(2, 65, 40), (5, 30, 110), (7, 15, 170), (9, 50, 220), (13, 25, 290), (16, 70, 320), (20, 10, 20), (23, 40, 260), (26, 5, 75), (29, 35, 140)]


def sentence(body):
    checksum = reduce(lambda total, character: total ^ ord(character), body, 0)
    return f"${body}*{checksum:02X}\r\n"


def degrees_minutes(value, digits, positive, negative):
    hemisphere = positive if value >= 0 else negative
    value = abs(value)
    degrees = int(value)
    minutes = (value - degrees) * 60
    return f"{degrees:0{digits}d}{minutes:07.4f}", hemisphere


def epoch(time, longitude, latitude, speed, course, generator, used_count):
    clock = time.strftime("%H%M%S") + ".000"
    date = time.strftime("%d%m%y")
    latitude_text, north_south = degrees_minutes(latitude, 2, "N", "S")
    longitude_text, east_west = degrees_minutes(longitude, 3, "E", "W")
    used = [prn for prn, _, _ in SATELLITES[:used_count]]
    hdop = 0.9 if used_count >= 8 else 1.8
    lines = []
    if used_count >= 3:
        lines.append(sentence(f"GPGGA,{clock},{latitude_text},{north_south},{longitude_text},{east_west},1,{used_count:02d},{hdop:.1f},12.0,M,47.0,M,,"))
    else:
        lines.append(sentence(f"GPGGA,{clock},,,,,0,{used_count:02d},,,M,,M,,"))
    prns = ",".join(f"{prn:02d}" for prn in used) + "," * (12 - len(used))
    mode = 3 if used_count >= 4 else 2 if used_count == 3 else 1
    lines.append(sentence(f"GPGSA,A,{mode},{prns},{hdop + 0.8:.1f},{hdop:.1f},{hdop + 0.5:.1f}"))
    groups = [SATELLITES[index:index + 4] for index in range(0, len(SATELLITES), 4)]
    for number, group in enumerate(groups, start=1):
        fields = []
        for prn, elevation, azimuth in group:
            drift = (azimuth + (time.minute * 60 + time.second) / 240) % 360
            snr = max(0, min(50, 18 + elevation // 3 + generator.randint(-3, 3))) if prn in used else generator.choice(["", "12", "15"])
            fields.append(f"{prn:02d},{elevation:02d},{drift:03.0f},{snr}")
        lines.append(sentence(f"GPGSV,{len(groups)},{number},{len(SATELLITES)},{','.join(fields)}"))
    status = "A" if used_count >= 3 else "V"
    if status == "A":
        lines.append(sentence(f"GPRMC,{clock},A,{latitude_text},{north_south},{longitude_text},{east_west},{speed / 0.514444:.1f},{course:.1f},{date},,"))
    else:
        lines.append(sentence(f"GPRMC,{clock},V,,,,,,,{date},,"))
    return lines


def main():
    parser = argparse.ArgumentParser(description="Write an NMEA file of a walk in a circle, for GISmo's replay.nmea")
    parser.add_argument("--centre", type=float, nargs=2, metavar=("EAST", "NORTH"), default=(545000, 258000), help="BNG metres")
    parser.add_argument("--radius", type=float, default=150, help="metres")
    parser.add_argument("--speed", type=float, default=1.4, help="metres per second")
    parser.add_argument("--acquire", type=int, default=20, help="seconds before the first fix")
    parser.add_argument("--output", type=Path, default=Path("replay.nmea"))
    args = parser.parse_args()

    generator = random.Random(1)
    start = datetime.datetime(2026, 11, 14, 10, 0, 0, tzinfo=datetime.timezone.utc)
    circumference = 2 * math.pi * args.radius
    walk_seconds = int(circumference / args.speed)
    lines = []
    for second in range(args.acquire + walk_seconds):
        time = start + datetime.timedelta(seconds=second)
        used_count = min(8, second * 8 // max(args.acquire, 1))
        walked = max(0, second - args.acquire) * args.speed
        angle = walked / args.radius
        east = args.centre[0] + args.radius * math.sin(angle)
        north = args.centre[1] - args.radius * math.cos(angle)
        course = math.degrees(angle + math.pi / 2) % 360
        longitude, latitude = bng_to_wgs84.transform(east, north)
        speed = args.speed if second >= args.acquire else 0
        lines += epoch(time, longitude, latitude, speed, course, generator, used_count)
    args.output.write_text("".join(lines), newline="")
    print(f"{args.output}: {args.acquire + walk_seconds} seconds")


if __name__ == "__main__":
    main()
