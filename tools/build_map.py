# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy", "pillow", "pyproj"]
# ///

import argparse
import math
import os
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

import numpy
from PIL import Image, ImageDraw

import gmap
from bng import PROJECTION
from osm import load_paths
from sources import NLS_LAYERS, NlsSource, SheetSource, named_tiff_sheets, ozi_sheets, snap_to_colours, world_file_sheet

BLOCK = 8

PATH_STYLES = {
    "footpath": ((220, 60, 20), 2, 8.0, 6.0),
    "track": ((140, 90, 40), 2, 16.0, 8.0),
}


def dashes(points, on, off):
    pattern = on + off
    position = 0.0
    for start, end in zip(points[:-1], points[1:]):
        length = math.hypot(end[0] - start[0], end[1] - start[1])
        travelled = 0.0
        while travelled < length:
            phase = position % pattern
            step = min(length - travelled, (on - phase) if phase < on else (pattern - phase))
            if phase < on and step > 0:
                fraction_start = travelled / length
                fraction_end = (travelled + step) / length
                yield (
                    (start[0] + (end[0] - start[0]) * fraction_start, start[1] + (end[1] - start[1]) * fraction_start),
                    (start[0] + (end[0] - start[0]) * fraction_end, start[1] + (end[1] - start[1]) * fraction_end),
                )
            travelled += max(step, 1e-9)
            position += max(step, 1e-9)


def draw_paths(pixels, paths, level, column, row):
    metres_per_pixel = level.metres_per_pixel
    west = column * level.span
    north = (row + 1) * level.span
    margin = 4 * metres_per_pixel
    touching = [path for path in paths if path.east >= west - margin and path.west <= west + level.span + margin and path.north >= north - level.span - margin and path.south <= north + margin]
    if not touching:
        return
    layer = Image.new("RGBA", (gmap.TILE, gmap.TILE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(layer)
    for path in touching:
        colour, width, on, off = PATH_STYLES[path.style]
        screen = numpy.column_stack([(path.points[:, 0] - west) / metres_per_pixel, (north - path.points[:, 1]) / metres_per_pixel])
        for start, end in dashes(screen, on / metres_per_pixel, off / metres_per_pixel):
            draw.line([start, end], fill=colour + (255,), width=width)
    overlay = numpy.asarray(layer)
    drawn = (overlay[..., 3] >= 128) & (pixels[..., 3] >= 128)
    pixels[drawn, :3] = overlay[drawn, :3]


def downsample(pack_path, finer, column, row):
    quad = numpy.zeros((gmap.TILE * 2, gmap.TILE * 2, 4), numpy.uint8)
    with open(pack_path, "rb") as file:
        for child_y, child_row in enumerate((row * 2 + 1, row * 2)):
            for child_x, child_column in enumerate((column * 2, column * 2 + 1)):
                entry = finer.index.get((child_column, child_row))
                if entry is None:
                    continue
                file.seek(entry[0])
                child = gmap.decode_tile(file.read(entry[1]))
                quad[child_y * gmap.TILE:(child_y + 1) * gmap.TILE, child_x * gmap.TILE:(child_x + 1) * gmap.TILE] = child

    blocks = quad.reshape(gmap.TILE, 2, gmap.TILE, 2, 4).astype(numpy.uint32)
    covered = (blocks[..., 3] >= 128).astype(numpy.uint32)
    count = covered.sum(axis=(1, 3))
    sums = (blocks[..., :3] * covered[..., None]).sum(axis=(1, 3))
    pixels = numpy.zeros((gmap.TILE, gmap.TILE, 4), numpy.uint8)
    has_any = count > 0
    pixels[has_any, :3] = (sums[has_any] + count[has_any, None] // 2) // count[has_any, None]
    pixels[count >= 2, 3] = 255
    snap_to_colours(pixels, quad[quad[..., 3] >= 128, :3])
    return pixels


def build_block(job):
    level, sources, pack_path, finer, tiles, paths = job
    results = []
    for column, row in tiles:
        pixels = downsample(pack_path, finer, column, row) if finer is not None else numpy.zeros((gmap.TILE, gmap.TILE, 4), numpy.uint8)
        for source in sources:
            fresh = source.render(level, column, row)
            if fresh is not None:
                gmap.paste(pixels, fresh, 0, 0)
        if paths:
            draw_paths(pixels, paths, level, column, row)
        data = gmap.encode_tile(pixels)
        if data is not None:
            results.append((column, row, data))
    return results


def level_for(millimetres_per_pixel, west, south, east, north):
    span = millimetres_per_pixel * gmap.TILE // 1000
    first_column, last_column = int(west // span), int(-(-east // span))
    first_row, last_row = int(south // span), int(-(-north // span))
    return gmap.Level(millimetres_per_pixel, first_column, first_row, last_column - first_column, last_row - first_row)


def blocks_of(level):
    for block_row in range(level.first_row, level.first_row + level.rows, BLOCK):
        for block_column in range(level.first_column, level.first_column + level.columns, BLOCK):
            yield [
                (column, row)
                for row in range(block_row, min(block_row + BLOCK, level.first_row + level.rows))
                for column in range(block_column, min(block_column + BLOCK, level.first_column + level.columns))
            ]


def block_bounds(level, tiles, margin=0):
    span = level.span
    columns = [column for column, _ in tiles]
    rows = [row for _, row in tiles]
    return min(columns) * span - margin, min(rows) * span - margin, (max(columns) + 1) * span + margin, (max(rows) + 1) * span + margin


def level_of_source(source, base):
    ratio = source.millimetres_per_pixel / base
    if ratio <= 1.01:
        return base
    return base * 2 ** math.ceil(math.log2(ratio / 1.01))


def build(output, name, attribution, sources, bbox=None, metres_per_pixel=None, paths_gpkg=None, max_tiles=2, workers=None):
    extents = [source.bounds() for source in sources if source.bounds()[0] is not None]
    if bbox:
        west, south, east, north = bbox
    else:
        west, south = min(extent[0] for extent in extents), min(extent[1] for extent in extents)
        east, north = max(extent[2] for extent in extents), max(extent[3] for extent in extents)

    base = round(metres_per_pixel * 1000) if metres_per_pixel else min(source.millimetres_per_pixel for source in sources)
    assigned = [(level_of_source(source, base), source) for source in sources]
    coarsest = max(millimetres for millimetres, _ in assigned)

    paths = load_paths(paths_gpkg, west, south, east, north) if paths_gpkg else []
    if paths_gpkg:
        print(f"{len(paths)} paths and tracks", flush=True)

    partial = Path(output).with_name(Path(output).name + ".partial")
    writer = gmap.Writer(partial, f"name={name}\nattribution={attribution}\nprojection={PROJECTION}\n")
    finer = None
    millimetres_per_pixel = base
    with ProcessPoolExecutor(workers or os.cpu_count()) as executor:
        while True:
            level = level_for(millimetres_per_pixel, west, south, east, north)
            writer.add_level(level)
            level_sources = sorted((source for millimetres, source in assigned if millimetres == millimetres_per_pixel), key=lambda source: -source.millimetres_per_pixel)
            jobs = []
            for tiles in blocks_of(level):
                bounds = block_bounds(level, tiles)
                block_sources = [narrowed for narrowed in (source.within(*bounds) for source in level_sources) if narrowed is not None]
                block_paths = []
                if paths and millimetres_per_pixel == base:
                    margin_bounds = block_bounds(level, tiles, level.span)
                    block_paths = [path for path in paths if path.east > margin_bounds[0] and path.west < margin_bounds[2] and path.north > margin_bounds[1] and path.south < margin_bounds[3]]
                if block_sources or finer is not None:
                    jobs.append((level, block_sources, str(partial), finer, tiles, block_paths))
            for results in executor.map(build_block, jobs):
                for column, row, data in results:
                    writer.add_tile(level, column, row, data)
            writer.flush()
            print(f"{level.metres_per_pixel:g} m/px: {level.columns}x{level.rows} tiles, {len(level.index)} stored", flush=True)
            if level.columns <= max_tiles and level.rows <= max_tiles and millimetres_per_pixel >= coarsest:
                break
            finer = level
            millimetres_per_pixel *= 2
    writer.close()
    partial.replace(output)
    print(f"{output}: {Path(output).stat().st_size / 1e6:.1f} MB", flush=True)


def main():
    parser = argparse.ArgumentParser(description="Build a GISmo map pack (.gmap) on the British National Grid from raster maps")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--attribution", default="")
    parser.add_argument("--bbox", type=float, nargs=4, metavar=("WEST", "SOUTH", "EAST", "NORTH"), help="BNG metres; required for --nls")
    parser.add_argument("--os-tiles", type=Path, nargs="+", default=[], metavar="ZIP_OR_FOLDER", help="GeoTIFFs named by grid square or quarter (TL, TL45NW), eg: OS OpenMap Local, 1:250 000 Scale Colour Raster")
    parser.add_argument("--world-file", type=Path, nargs="+", default=[], metavar="IMAGE", help="BNG rasters with a .tfw world file beside them, eg: MiniScale")
    parser.add_argument("--ozi", type=Path, nargs="+", default=[], metavar="FOLDER", help="folders of OziExplorer .map/.ozf2 sheets")
    parser.add_argument("--nls", choices=sorted(NLS_LAYERS), help="National Library of Scotland historic OS layer")
    parser.add_argument("--nls-cache", type=Path, default=Path(__file__).parent.parent / "downloads" / "nls")
    parser.add_argument("--supersample", type=int, default=2, help="NLS rendering supersampling")
    parser.add_argument("--paths", type=Path, nargs="+", metavar="GPKG", help="Geofabrik OpenStreetMap .gpkg extracts: draw footpaths and tracks on the finest level")
    parser.add_argument("--metres-per-pixel", type=float, help="finest level; defaults to the finest source")
    parser.add_argument("--max-tiles", type=int, default=2, help="stop adding coarser levels once a level fits in this many tiles each way")
    parser.add_argument("--workers", type=int)
    args = parser.parse_args()

    sources = []
    sheets = named_tiff_sheets(args.os_tiles, args.bbox) + [world_file_sheet(path) for path in args.world_file]
    for folder in args.ozi:
        sheets += ozi_sheets(folder)
    if args.bbox:
        sheets = [sheet for sheet in sheets if sheet.touches(*args.bbox)]
    by_resolution = {}
    for sheet in sheets:
        by_resolution.setdefault(sheet.millimetres_per_pixel, []).append(sheet)
    sources += [SheetSource(group) for _, group in sorted(by_resolution.items())]
    if args.nls:
        if not args.bbox:
            parser.error("--nls needs --bbox")
        sources.append(NlsSource(args.nls, args.nls_cache.resolve(), args.supersample, *args.bbox))
    if not sources:
        parser.error("no map sources found")

    build(args.output, args.name, args.attribution, sources, args.bbox, args.metres_per_pixel, args.paths, args.max_tiles, args.workers)


if __name__ == "__main__":
    main()
