# /// script
# requires-python = ">=3.10"
# dependencies = ["numpy", "pillow", "pyproj"]
# ///

import argparse
import json
import math
import sqlite3
import struct
from pathlib import Path

from bng import lonlat_bounds, wgs84_to_bng
from osm import ATTRIBUTION, gpkg_location

TABLES = [
    "gis_osm_pois_free",
    "gis_osm_pois_a_free",
    "gis_osm_pofw_free",
    "gis_osm_pofw_a_free",
    "gis_osm_natural_free",
    "gis_osm_natural_a_free",
    "gis_osm_water_a_free",
    "gis_osm_places_free",
    "gis_osm_traffic_free",
]

UNNAMED_KINDS = {"playground", "post_box", "drinking_water", "viewpoint", "picnic_site", "shelter", "pitch", "park", "water", "wetland", "memorial", "monument", "artwork", "tower", "windmill", "water_mill", "waterfall", "spring", "beach", "camp_site", "stile", "gate", "toilet", "fountain"}


PLAY, HISTORY, NATURE, SIGHTS = range(4)
CHURCH = (HISTORY, "Church", False)

CURATED = {
    "playground": (PLAY, "Playground", True),
    "park": (PLAY, "Park", False),
    "picnic_site": (PLAY, "Picnic spot", True),
    "ice_rink": (PLAY, "Ice rink", False),
    "monument": (HISTORY, "Monument", True),
    "memorial": (HISTORY, "Memorial", True),
    "ruins": (HISTORY, "Ruins", True),
    "archaeological": (HISTORY, "Ancient site", True),
    "castle": (HISTORY, "Castle", True),
    "windmill": (HISTORY, "Windmill", True),
    "water_mill": (HISTORY, "Water mill", True),
    "wayside_cross": (HISTORY, "Wayside cross", True),
    "tower": (HISTORY, "Tower", False),
    "christian": CHURCH,
    "christian_anglican": CHURCH,
    "christian_catholic": CHURCH,
    "christian_methodist": CHURCH,
    "christian_evangelical": CHURCH,
    "christian_lutheran": CHURCH,
    "viewpoint": (NATURE, "Viewpoint", True),
    "peak": (NATURE, "Hill top", True),
    "spring": (NATURE, "Spring", True),
    "cave_entrance": (NATURE, "Cave", True),
    "waterfall": (NATURE, "Waterfall", True),
    "beach": (NATURE, "Beach", True),
    "tree": (NATURE, "Special tree", False),
    "water": (NATURE, "Pond or lake", False),
    "wetland": (NATURE, "Wetland", False),
    "reservoir": (NATURE, "Reservoir", False),
    "museum": (SIGHTS, "Museum", True),
    "artwork": (SIGHTS, "Artwork", True),
    "attraction": (SIGHTS, "Attraction", False),
    "fountain": (SIGHTS, "Fountain", True),
    "library": (SIGHTS, "Library", False),
    "zoo": (SIGHTS, "Zoo", False),
    "theme_park": (SIGHTS, "Theme park", False),
    "observation_tower": (SIGHTS, "Lookout tower", True),
    "water_tower": (SIGHTS, "Water tower", True),
    "arts_centre": (SIGHTS, "Arts centre", False),
    "marketplace": (SIGHTS, "Market", False),
}

PLACES_MAGIC = b"GISMOPLC"
PLACES_VERSION = 1
PLACES_HEADER = struct.Struct("<8sHHIiiIIIIIIII")
PLACE_RECORD = struct.Struct("<iiHHI")
CELL_SIZE = 1000
DUPLICATE_METRES = 30


def curated_places(places):
    kept = []
    seen = {}
    for place in places:
        entry = CURATED.get(place["kind"])
        if entry is None:
            continue
        category, label, unnamed_ok = entry
        if not place["name"] and not unnamed_ok:
            continue
        east, north = wgs84_to_bng.transform(place["longitude"], place["latitude"])
        key = (place["name"], label)
        duplicate = any(math.hypot(east - other_east, north - other_north) < DUPLICATE_METRES for other_east, other_north in seen.get(key, []))
        if duplicate:
            continue
        seen.setdefault(key, []).append((east, north))
        kept.append((round(east), round(north), label, category, place["name"]))
    return kept


def write_gplaces(places, output):
    labels = sorted({(category, label) for _, _, label, category, _ in places}, key=lambda entry: (entry[0], entry[1]))
    kind_numbers = {label: number for number, (_, label) in enumerate(labels)}
    west = min(east for east, _, _, _, _ in places) // CELL_SIZE * CELL_SIZE
    south = min(north for _, north, _, _, _ in places) // CELL_SIZE * CELL_SIZE
    columns = (max(east for east, _, _, _, _ in places) - west) // CELL_SIZE + 1
    rows = (max(north for _, north, _, _, _ in places) - south) // CELL_SIZE + 1
    cell_of = lambda east, north: (north - south) // CELL_SIZE * columns + (east - west) // CELL_SIZE
    ordered = sorted(places, key=lambda place: (cell_of(place[0], place[1]), place[4].lower(), place[2]))

    kinds = b"".join(struct.pack("<BB", category, len(label.encode())) + label.encode() for category, label in labels)
    kinds += bytes(-(PLACES_HEADER.size + len(kinds)) % 4)
    names = bytearray(b"\0")
    name_offsets = {"": 0}
    records = bytearray()
    for east, north, label, _, name in ordered:
        if name not in name_offsets:
            name_offsets[name] = len(names)
            names += name.encode() + b"\0"
        records += PLACE_RECORD.pack(east, north, kind_numbers[label], 0, name_offsets[name])
    starts = [0] * (columns * rows + 1)
    for east, north, _, _, _ in ordered:
        starts[cell_of(east, north) + 1] += 1
    for index in range(1, len(starts)):
        starts[index] += starts[index - 1]
    cells = struct.pack(f"<{len(starts)}I", *starts)

    kinds_offset = PLACES_HEADER.size
    cells_offset = kinds_offset + len(kinds)
    records_offset = cells_offset + len(cells)
    names_offset = records_offset + len(records)
    header = PLACES_HEADER.pack(PLACES_MAGIC, PLACES_VERSION, len(labels), len(ordered), west, south, CELL_SIZE, columns, rows, kinds_offset, cells_offset, records_offset, names_offset, len(names))
    output.write_bytes(header + kinds + cells + bytes(records) + bytes(names))
    by_category = [sum(1 for place in ordered if place[3] == category) for category in range(4)]
    print(f"{output}: {len(ordered)} places (play {by_category[0]}, history {by_category[1]}, nature {by_category[2]}, sights {by_category[3]}), {output.stat().st_size / 1e3:.0f} KB")


def place_rows(gpkgs):
    for gpkg in gpkgs:
        database = sqlite3.connect(gpkg)
        for table in TABLES:
            if database.execute("select 1 from sqlite_master where name = ?", (table,)).fetchone():
                yield from database.execute(f"select geom, fclass, name from {table}")
        database.close()


def export_places(gpkgs, bbox, output, curated_output=None):
    min_lon, min_lat, max_lon, max_lat = lonlat_bounds(*bbox)

    places = []
    seen = set()
    for blob, kind, name in place_rows(gpkgs):
        point = gpkg_location(blob)
        if point is None or not (min_lon <= point[0] <= max_lon and min_lat <= point[1] <= max_lat):
            continue
        name = (name or "").strip()
        if not name and kind not in UNNAMED_KINDS:
            continue
        key = (name, kind, round(point[0], 4), round(point[1], 4))
        if key in seen:
            continue
        seen.add(key)
        places.append({"name": name, "kind": kind, "latitude": round(point[1], 6), "longitude": round(point[0], 6)})

    places.sort(key=lambda place: (place["name"] == "", place["name"].lower(), place["kind"]))
    output.write_text(json.dumps({"attribution": f"Places {ATTRIBUTION}", "places": places}, separators=(",", ":")))
    named = sum(1 for place in places if place["name"])
    print(f"{output}: {len(places)} places ({named} named), {output.stat().st_size / 1e3:.0f} KB")
    if curated_output:
        write_gplaces(curated_places(places), curated_output)


def main():
    parser = argparse.ArgumentParser(description="Export OpenStreetMap places for the GISmo mission planner")
    parser.add_argument("gpkg", type=Path, nargs="+")
    parser.add_argument("--bbox", type=float, nargs=4, metavar=("WEST", "SOUTH", "EAST", "NORTH"), required=True, help="BNG metres")
    parser.add_argument("--output", type=Path, required=True, help="every named place, for the planner (.json)")
    parser.add_argument("--curated", type=Path, help="places worth visiting, for the device (.gplaces)")
    args = parser.parse_args()
    export_places(args.gpkg, args.bbox, args.output, args.curated)


if __name__ == "__main__":
    main()
