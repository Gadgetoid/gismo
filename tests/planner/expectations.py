# /// script
# requires-python = ">=3.10"
# dependencies = ["numpy", "pillow", "pyproj"]
# ///

import hashlib
import json
import sys
from pathlib import Path

import numpy
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import gmap
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "host"))
from check_routes import dijkstra
from make_routes import read_graph
from make_hint import to_bmp565


def main():
    pack_path = sys.argv[1]
    reader = gmap.Reader(pack_path)
    tiles = {}
    for number, level in enumerate(reader.levels):
        for column, row in sorted(level.index)[:40]:
            pixels = reader.tile_pixels(level, column, row)
            tiles[f"{number}/{column}/{row}"] = hashlib.sha1(pixels.tobytes()).hexdigest()
    gradient = numpy.zeros((240, 320, 3), numpy.uint8)
    gradient[..., 0] = numpy.arange(320)[None, :] * 255 // 319
    gradient[..., 1] = numpy.arange(240)[:, None] * 255 // 239
    gradient[..., 2] = 128
    bmp = to_bmp565(Image.fromarray(gradient, "RGB"))
    nodes, offsets, edges = read_graph(sys.argv[2])
    connected = [index for index in range(len(nodes)) if offsets[index + 1] > offsets[index]]
    routes = []
    for step in range(12):
        start = connected[(step * 7919) % len(connected)]
        goal = connected[(step * 104729 + 13) % len(connected)]
        routes.append([start, goal, dijkstra(offsets, edges, start, goal)])
    print(json.dumps({"tiles": tiles, "bmp": hashlib.sha1(bmp).hexdigest(), "routes": routes}))


if __name__ == "__main__":
    main()
