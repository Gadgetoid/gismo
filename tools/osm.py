import sqlite3
import struct

import numpy

from bng import lonlat_bounds, wgs84_to_bng

GEOFABRIK_URL = "https://download.geofabrik.de"
ATTRIBUTION = "(c) OpenStreetMap contributors"

FOOTPATH_CLASSES = {"footway", "path", "bridleway", "cycleway", "steps", "pedestrian"}


def geofabrik_url(region):
    return f"{GEOFABRIK_URL}/{region.strip('/')}-latest-free.gpkg.zip"


def geometry(blob):
    flags = blob[3]
    envelope = {0: 0, 1: 32, 2: 48, 3: 48, 4: 64}[(flags >> 1) & 7]
    return memoryview(blob)[8 + envelope:], envelope


def gpkg_lines(blob):
    return wkb_lines(geometry(blob)[0])


def wkb_lines(wkb):
    order = "<" if wkb[0] == 1 else ">"
    kind = struct.unpack_from(order + "I", wkb, 1)[0] % 1000
    if kind == 2:
        count = struct.unpack_from(order + "I", wkb, 5)[0]
        return [numpy.frombuffer(wkb, order + "f8", count * 2, 9).reshape(count, 2)]
    if kind == 5:
        lines = []
        parts = struct.unpack_from(order + "I", wkb, 5)[0]
        offset = 9
        for _ in range(parts):
            count = struct.unpack_from(order + "I", wkb, offset + 5)[0]
            lines.append(numpy.frombuffer(wkb, order + "f8", count * 2, offset + 9).reshape(count, 2))
            offset += 9 + count * 16
        return lines
    return []


def gpkg_location(blob):
    wkb, envelope = geometry(blob)
    if envelope:
        minx, maxx, miny, maxy = struct.unpack_from("<dddd", blob, 8)
        return (minx + maxx) / 2, (miny + maxy) / 2
    order = "<" if wkb[0] == 1 else ">"
    if struct.unpack_from(order + "I", wkb, 1)[0] == 1:
        return struct.unpack_from(order + "dd", wkb, 5)
    return None


def roads(gpkgs, with_speed=False):
    columns = "geom, fclass, maxspeed" if with_speed else "geom, fclass"
    for gpkg in gpkgs:
        database = sqlite3.connect(gpkg)
        yield from database.execute(f"select {columns} from gis_osm_roads_free")
        database.close()


def line_touches(line, min_lon, min_lat, max_lon, max_lat):
    return not (line[:, 0].max() < min_lon or line[:, 0].min() > max_lon or line[:, 1].max() < min_lat or line[:, 1].min() > max_lat)


class Path:
    def __init__(self, style, points):
        self.style = style
        self.points = points
        self.west, self.south = points.min(axis=0)
        self.east, self.north = points.max(axis=0)


def load_paths(gpkgs, west, south, east, north):
    bounds = lonlat_bounds(west, south, east, north, 0.01)
    paths = []
    for blob, fclass in roads(gpkgs):
        style = "footpath" if fclass in FOOTPATH_CLASSES else "track" if fclass.startswith("track") else None
        if style is None:
            continue
        for line in gpkg_lines(blob):
            if not line_touches(line, *bounds):
                continue
            east_values, north_values = wgs84_to_bng.transform(line[:, 0], line[:, 1])
            paths.append(Path(style, numpy.column_stack([east_values, north_values])))
    return paths
