# /// script
# requires-python = ">=3.10"
# dependencies = ["numpy", "pillow", "pyproj"]
# ///

import argparse
import math
import sqlite3
import struct
from pathlib import Path

from bng import lonlat_bounds, wgs84_to_bng
from osm import gpkg_lines, line_touches, roads

MAGIC = b"GISMORTE"
VERSION = 1
HEADER = struct.Struct("<8sHHII")

WALKING_COST = {
    "footway": 1.0,
    "path": 1.0,
    "pedestrian": 1.0,
    "steps": 1.1,
    "bridleway": 1.0,
    "cycleway": 1.05,
    "track": 1.0,
    "track_grade1": 1.0,
    "track_grade2": 1.0,
    "track_grade3": 1.1,
    "track_grade4": 1.2,
    "track_grade5": 1.3,
    "living_street": 1.1,
    "residential": 1.3,
    "service": 1.8,
    "unclassified": 1.5,
    "tertiary": 1.6,
    "tertiary_link": 1.6,
    "secondary": 1.8,
    "secondary_link": 1.8,
    "primary": 2.0,
    "primary_link": 2.0,
    "trunk": 3.0,
    "trunk_link": 3.0,
}
ROAD_CLASSES = {"residential", "service", "unclassified", "tertiary", "tertiary_link", "secondary", "secondary_link", "primary", "primary_link", "trunk", "trunk_link"}
TRUNK_CLASSES = {"trunk", "trunk_link"}
FAST_ROAD_KPH = 64
FAST_ROAD_FACTOR = 1.5


def walking_factor(fclass, maxspeed):
    factor = WALKING_COST.get(fclass)
    if factor is None:
        return None
    speed = maxspeed or 0
    if fclass in TRUNK_CLASSES and (speed == 0 or speed > FAST_ROAD_KPH):
        return None
    if fclass in ROAD_CLASSES and speed >= FAST_ROAD_KPH:
        factor *= FAST_ROAD_FACTOR
    return factor


def build_graph(gpkgs, west, south, east, north):
    bounds = lonlat_bounds(west, south, east, north)
    node_ids = {}
    coordinates = []
    edges = {}

    def node(longitude, latitude):
        key = (round(longitude, 7), round(latitude, 7))
        index = node_ids.get(key)
        if index is None:
            index = len(coordinates)
            node_ids[key] = index
            coordinates.append(key)
        return index

    for blob, fclass, maxspeed in roads(gpkgs, with_speed=True):
        factor = walking_factor(fclass, maxspeed)
        if factor is None:
            continue
        for line in gpkg_lines(blob):
            if not line_touches(line, *bounds):
                continue
            previous = None
            for longitude, latitude in line:
                current = node(float(longitude), float(latitude))
                if previous is not None and previous != current:
                    for start, end in ((previous, current), (current, previous)):
                        key = (start, end)
                        edges[key] = min(edges.get(key, factor), factor)
                previous = current

    eastings, northings = wgs84_to_bng.transform([lon for lon, _ in coordinates], [lat for _, lat in coordinates])
    decimetres = [(int(round(e * 10)), int(round(n * 10))) for e, n in zip(eastings, northings)]

    adjacency = [[] for _ in decimetres]
    for (start, end), factor in edges.items():
        ax, ay = decimetres[start]
        bx, by = decimetres[end]
        length = math.hypot(bx - ax, by - ay)
        cost = max(1, math.ceil(length * factor))
        adjacency[start].append((end, cost))
    return decimetres, adjacency


def write_graph(path, decimetres, adjacency):
    edge_count = sum(len(targets) for targets in adjacency)
    with open(path, "wb") as output:
        output.write(HEADER.pack(MAGIC, VERSION, 0, len(decimetres), edge_count))
        for east, north in decimetres:
            output.write(struct.pack("<ii", east, north))
        offset = 0
        for targets in adjacency:
            output.write(struct.pack("<I", offset))
            offset += len(targets)
        output.write(struct.pack("<I", offset))
        for targets in adjacency:
            for target, cost in sorted(targets):
                output.write(struct.pack("<II", target, cost))


def read_graph(path):
    data = Path(path).read_bytes()
    magic, version, _, node_count, edge_count = HEADER.unpack_from(data)
    if magic != MAGIC or version != VERSION:
        raise ValueError(f"{path}: not a GISmo route graph")
    position = HEADER.size
    nodes = [struct.unpack_from("<ii", data, position + index * 8) for index in range(node_count)]
    position += node_count * 8
    offsets = struct.unpack_from(f"<{node_count + 1}I", data, position)
    position += (node_count + 1) * 4
    edges = [struct.unpack_from("<II", data, position + index * 8) for index in range(edge_count)]
    return nodes, offsets, edges


def main():
    parser = argparse.ArgumentParser(description="Build a GISmo walking route graph (.groute) from a Geofabrik OpenStreetMap .gpkg")
    parser.add_argument("gpkg", type=Path, nargs="+")
    parser.add_argument("--bbox", type=float, nargs=4, metavar=("WEST", "SOUTH", "EAST", "NORTH"), required=True, help="BNG metres")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    decimetres, adjacency = build_graph(args.gpkg, *args.bbox)
    write_graph(args.output, decimetres, adjacency)
    edge_count = sum(len(targets) for targets in adjacency)
    print(f"{args.output}: {len(decimetres)} nodes, {edge_count} edges, {args.output.stat().st_size / 1e6:.1f} MB")


if __name__ == "__main__":
    main()
