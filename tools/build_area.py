# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy", "pillow", "pyproj"]
# ///

import argparse
import tomllib
from pathlib import Path

import fetch
from bng import squares_touching
from build_map import build
from make_places import export_places
from make_routes import build_graph, write_graph
from osm import ATTRIBUTION as OSM_ATTRIBUTION
from sources import NLS_ATTRIBUTION, NlsSource, SheetSource, named_tiff_sheets, world_file_sheet

OS_ATTRIBUTION = "Contains OS data (c) Crown copyright and database right"


def expand(bbox, margin):
    west, south, east, north = bbox
    return [west - margin, south - margin, east + margin, north + margin]


def osm_extracts(area, downloads):
    regions = area["osm"] if isinstance(area["osm"], list) else [area["osm"]]
    return [fetch.osm(region, downloads) for region in regions]


def build_pack(area, pack, stem, output, downloads, workers):
    bbox = expand(area["bbox"], pack.get("margin", 0))
    suffix = pack.get("suffix")
    path = output / (f"{stem}-{suffix}.gmap" if suffix else f"{stem}.gmap")
    if path.exists():
        print(f"{path}: exists, skipped", flush=True)
        return
    name = pack.get("name", area["name"])
    kind = pack["source"]
    paths = None
    if kind == "os-local":
        zips = [fetch.os_local(square, downloads) for square in squares_touching(*bbox)]
        sources = [SheetSource(named_tiff_sheets(zips, bbox))]
        attribution = OS_ATTRIBUTION
        if pack.get("paths"):
            paths = osm_extracts(area, downloads)
            attribution += f". Paths {OSM_ATTRIBUTION}"
        metres_per_pixel = pack.get("metres_per_pixel", 2)
    elif kind == "os-overview":
        sources = [SheetSource(named_tiff_sheets([fetch.os_250k(downloads)], bbox)), SheetSource([world_file_sheet(fetch.os_miniscale(downloads))])]
        attribution = OS_ATTRIBUTION
        metres_per_pixel = pack.get("metres_per_pixel")
    elif kind == "nls":
        sources = [NlsSource(pack["layer"], downloads / "nls", pack.get("supersample", 2), *bbox)]
        attribution = NLS_ATTRIBUTION
        metres_per_pixel = pack.get("metres_per_pixel")
    else:
        raise SystemExit(f"unknown map source {kind!r}: os-local, os-overview or nls")
    print(f"building {path}", flush=True)
    build(path, name, pack.get("attribution", attribution), sources, bbox, metres_per_pixel, paths, pack.get("max_tiles", 2), workers)


def route_tiles(area, stem, output):
    tile = area.get("route_tile")
    if not tile:
        yield output / f"{stem}.groute", area["bbox"]
        return
    overlap = area.get("route_overlap", tile // 4)
    west, south, east, north = area["bbox"]
    for tile_south in range(int(south // tile * tile), int(north), tile):
        for tile_west in range(int(west // tile * tile), int(east), tile):
            name = f"{stem}-{tile_west // 1000}-{tile_south // 1000}-{tile // 1000}.groute"
            yield output / name, expand([tile_west, tile_south, tile_west + tile, tile_south + tile], overlap)


def build_osm_extras(area, stem, output, downloads):
    gpkgs = osm_extracts(area, downloads)
    for routes, bbox in route_tiles(area, stem, output):
        if routes.exists():
            continue
        nodes, adjacency = build_graph(gpkgs, *bbox)
        if nodes:
            write_graph(routes, nodes, adjacency)
            print(f"{routes}: {len(nodes)} nodes, {routes.stat().st_size / 1e6:.1f} MB", flush=True)
    places = output / f"{stem}.places.json"
    curated = output / f"{stem}.gplaces"
    if not places.exists() or not curated.exists():
        export_places(gpkgs, area["bbox"], places, curated)


def main():
    parser = argparse.ArgumentParser(description="Build every map pack, the walking routes and the places for an area file")
    parser.add_argument("area", type=Path, help="area .toml")
    parser.add_argument("--output", type=Path, default=Path(__file__).parent.parent / "build" / "maps")
    parser.add_argument("--downloads", type=Path, default=fetch.DEFAULT_DOWNLOADS)
    parser.add_argument("--workers", type=int)
    args = parser.parse_args()

    area = tomllib.loads(args.area.read_text())
    stem = area.get("file", args.area.stem)
    args.output.mkdir(parents=True, exist_ok=True)
    for pack in area.get("maps", []):
        build_pack(area, pack, stem, args.output, args.downloads.resolve(), args.workers)
    if "osm" in area:
        build_osm_extras(area, stem, args.output, args.downloads.resolve())


if __name__ == "__main__":
    main()
