import functools
import io
import json
import math
import struct
import time
import urllib.error
import urllib.request
import zipfile
import zlib
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy
from PIL import Image

import gmap
from bng import bng_to_wgs84, named_bounds, wgs84_to_bng

Image.MAX_IMAGE_PIXELS = None

SHEET_CACHE = 8


class Sheet:
    def __init__(self, key, west, north, millimetres_per_pixel, width, height):
        self.key = key
        self.west = west
        self.north = north
        self.millimetres_per_pixel = millimetres_per_pixel
        self.width = width
        self.height = height

    @property
    def metres_per_pixel(self):
        return self.millimetres_per_pixel / 1000

    @property
    def east(self):
        return self.west + self.width * self.metres_per_pixel

    @property
    def south(self):
        return self.north - self.height * self.metres_per_pixel

    def touches(self, west, south, east, north):
        return self.east > west and self.west < east and self.north > south and self.south < north


def read_map_file(path):
    fields = {}
    lines = path.read_text("latin-1").splitlines()
    for line in lines:
        parts = [part.strip() for part in line.split(",")]
        if parts[0] in ("MMPXY", "MMPLL") and parts[1] == "1":
            fields[parts[0]] = (float(parts[2]), float(parts[3]))
        elif parts[0] in ("MM1B", "IWH"):
            fields[parts[0]] = parts[1:]
    if fields.get("MMPXY") != (0, 0):
        raise ValueError(f"{path}: first calibration point is not the top left pixel")
    east, north = wgs84_to_bng.transform(*fields["MMPLL"])
    millimetres_per_pixel = round(float(fields["MM1B"][0]) * 1000)
    width, height = int(fields["IWH"][1]), int(fields["IWH"][2])
    return Sheet(("ozf2", str(path.parent / lines[2].strip())), round(east), round(north), millimetres_per_pixel, width, height)


def ozi_sheets(folder):
    return [read_map_file(path) for path in sorted(Path(folder).glob("*.map"))]


def named_tiff_sheets(paths, bbox=None):
    sheets = []
    for path in map(Path, paths):
        if path.suffix.lower() == ".zip":
            with zipfile.ZipFile(path) as archive:
                for member in archive.namelist():
                    sheet = named_sheet(Path(member), ("zip", str(path), member), bbox, lambda member=member: Image.open(io.BytesIO(archive.read(member))))
                    if sheet:
                        sheets.append(sheet)
        elif path.is_dir():
            sheets += named_tiff_sheets(sorted(path.glob("*.tif")), bbox)
        else:
            sheet = named_sheet(path, ("file", str(path)), bbox, lambda path=path: Image.open(path))
            if sheet:
                sheets.append(sheet)
    return sheets


def named_sheet(path, key, bbox, open_image):
    if path.suffix.lower() != ".tif":
        return None
    bounds = named_bounds(path.stem.upper())
    if bounds is None:
        return None
    west, south, east, north = bounds
    if bbox and not (east > bbox[0] and west < bbox[2] and north > bbox[1] and south < bbox[3]):
        return None
    width, height = open_image().size
    return Sheet(key, west, north, round((east - west) * 1000 / width), width, height)


def world_file_sheet(image_path, world_path=None):
    image_path = Path(image_path)
    if world_path is None:
        candidates = [image_path.with_suffix(".tfw"), image_path.with_suffix(".tfwx")] + sorted(image_path.parent.rglob(f"{image_path.stem}.tfw"))
        world_path = next((path for path in candidates if path.exists()), None)
        if world_path is None:
            raise FileNotFoundError(f"{image_path}: no {image_path.stem}.tfw world file beside it or in a folder below")
    pixel_width, rotation_y, rotation_x, pixel_height, centre_east, centre_north = (float(value) for value in Path(world_path).read_text().split())
    if rotation_x or rotation_y or abs(pixel_width) != abs(pixel_height):
        raise ValueError(f"{world_path}: only north-up, square pixels are supported")
    width, height = Image.open(image_path).size
    return Sheet(("file", str(image_path)), centre_east - pixel_width / 2, centre_north + abs(pixel_height) / 2, round(pixel_width * 1000), width, height)


@functools.lru_cache(maxsize=SHEET_CACHE)
def sheet_pixels(key):
    kind = key[0]
    if kind == "ozf2":
        return read_ozf2(key[1])
    if kind == "zip":
        with zipfile.ZipFile(key[1]) as archive:
            image = Image.open(io.BytesIO(archive.read(key[2])))
            image.load()
    else:
        image = Image.open(key[1])
        image.load()
    if image.mode != "P":
        image = image.convert("RGB").quantize(255, dither=Image.Dither.NONE)
    palette = numpy.array(image.getpalette()[:768], numpy.uint8).reshape(-1, 3)
    return numpy.asarray(image), palette


def read_ozf2(path):
    data = Path(path).read_bytes()
    table_offset = struct.unpack_from("<I", data, len(data) - 4)[0]
    info_offset = struct.unpack_from("<I", data, table_offset)[0]
    width, height, tiles_x, tiles_y = struct.unpack_from("<IIHH", data, info_offset)
    palette = numpy.frombuffer(data, numpy.uint8, 1024, info_offset + 12).reshape(256, 4)[:, 2::-1].copy()
    offsets = struct.unpack_from(f"<{tiles_x * tiles_y + 1}I", data, info_offset + 12 + 1024)
    tile = 64
    indices = numpy.empty((tiles_y * tile, tiles_x * tile), numpy.uint8)
    for tile_y in range(tiles_y):
        for tile_x in range(tiles_x):
            number = tile_y * tiles_x + tile_x
            block = zlib.decompress(data[offsets[number]:offsets[number + 1]])
            rows = numpy.frombuffer(block, numpy.uint8).reshape(tile, tile)[::-1]
            indices[tile_y * tile:(tile_y + 1) * tile, tile_x * tile:(tile_x + 1) * tile] = rows
    return indices[:height, :width], palette


def snap_to_colours(pixels, allowed):
    covered = pixels[..., 3] >= 128
    if not covered.any():
        return
    choices = numpy.unique(allowed.reshape(-1, 3), axis=0).astype(numpy.int32)
    wanted, inverse = numpy.unique(pixels[covered, :3].astype(numpy.int32), axis=0, return_inverse=True)
    nearest = numpy.empty(len(wanted), numpy.int64)
    for start in range(0, len(wanted), 256):
        distances = ((wanted[start:start + 256, None, :] - choices[None, :, :]) ** 2).sum(axis=-1)
        nearest[start:start + 256] = distances.argmin(axis=1)
    pixels[covered, :3] = choices[nearest][inverse.reshape(-1)]


class SheetSource:
    def __init__(self, sheets):
        self.sheets = sheets
        self.millimetres_per_pixel = min(sheet.millimetres_per_pixel for sheet in sheets)

    def bounds(self):
        return min(sheet.west for sheet in self.sheets), min(sheet.south for sheet in self.sheets), max(sheet.east for sheet in self.sheets), max(sheet.north for sheet in self.sheets)

    def within(self, west, south, east, north):
        touching = [sheet for sheet in self.sheets if sheet.touches(west, south, east, north)]
        return SheetSource(touching) if touching else None

    def render(self, level, column, row):
        pixels = numpy.zeros((gmap.TILE, gmap.TILE, 4), numpy.uint8)
        west = column * level.span
        north = (row + 1) * level.span
        for sheet in self.sheets:
            if sheet.touches(west, north - level.span, west + level.span, north):
                paste_sheet(pixels, sheet, level, west, north)
        return pixels if pixels[..., 3].any() else None


def paste_sheet(pixels, sheet, level, west, north):
    indices, palette = sheet_pixels(sheet.key)
    metres_per_pixel = level.metres_per_pixel
    if sheet.millimetres_per_pixel == level.millimetres_per_pixel:
        x = round((sheet.west - west) / metres_per_pixel)
        y = round((north - sheet.north) / metres_per_pixel)
        left, top = max(-x, 0), max(-y, 0)
        right, bottom = min(gmap.TILE - x, sheet.width), min(gmap.TILE - y, sheet.height)
        pixels[top + y:bottom + y, left + x:right + x, :3] = palette[indices[top:bottom, left:right]]
        pixels[top + y:bottom + y, left + x:right + x, 3] = 255
        return

    scale = metres_per_pixel / sheet.metres_per_pixel
    left = (sheet.west - west) / metres_per_pixel
    top = (north - sheet.north) / metres_per_pixel
    target_left, target_top = max(round(left), 0), max(round(top), 0)
    target_right = min(round(left + sheet.width / scale), gmap.TILE)
    target_bottom = min(round(top + sheet.height / scale), gmap.TILE)
    if target_right <= target_left or target_bottom <= target_top:
        return
    source_left = (target_left - left) * scale
    source_top = (target_top - top) * scale
    source_right = (target_right - left) * scale
    source_bottom = (target_bottom - top) * scale
    crop_left = max(math.floor(source_left) - 2, 0)
    crop_top = max(math.floor(source_top) - 2, 0)
    crop_right = min(math.ceil(source_right) + 2, sheet.width)
    crop_bottom = min(math.ceil(source_bottom) + 2, sheet.height)
    crop = indices[crop_top:crop_bottom, crop_left:crop_right]
    image = Image.fromarray(palette[crop], "RGB")
    box = (source_left - crop_left, source_top - crop_top, source_right - crop_left, source_bottom - crop_top)
    resized = numpy.asarray(image.resize((target_right - target_left, target_bottom - target_top), Image.Resampling.LANCZOS, box=box))
    patch = numpy.zeros(resized.shape[:2] + (4,), numpy.uint8)
    patch[..., :3] = resized
    patch[..., 3] = 255
    snap_to_colours(patch, palette[numpy.unique(crop)])
    pixels[target_top:target_bottom, target_left:target_right] = patch


NLS_URL = "https://mapseries-tilesets.s3.amazonaws.com"
NLS_TILE = 256
NLS_FETCH_THREADS = 16
USER_AGENT = "GISmo/1.0 (offline maps for a kids' GPS game)"
NLS_ATTRIBUTION = "Reproduced with the permission of the National Library of Scotland"
SIX_INCH_FIRST_COUNTIES = ("lincolnshire", "northamptonshire", "bedfordshire", "hertfordshire", "essex", "suffolk", "norfolk", "huntingdonshire", "cambridgeshire")


class NlsLayer:
    def __init__(self, paths, zoom, metres_per_pixel, cleaned):
        self.paths = paths
        self.zoom = zoom
        self.metres_per_pixel = metres_per_pixel
        self.cleaned = cleaned


NLS_LAYERS = {
    "6inch": NlsLayer(("os/6inchsecond",), 16, 2, True),
    "6inch1880": NlsLayer(tuple(f"os/six-inch-{county}" for county in SIX_INCH_FIRST_COUNTIES), 16, 2, True),
    "1inch1890": NlsLayer(("1inch_2nd_ed",), 14, 4, True),
    "1inch1900": NlsLayer(("1inch_3rd_col_eng",), 14, 4, False),
    "1inch1940": NlsLayer(("os/newpopular",), 14, 4, False),
}

PAPER = numpy.array([242, 238, 214], numpy.float32)
INK = numpy.array([62, 52, 42], numpy.float32)
INK_LEVELS = 8
FAINT_INK = 0.18
LONELY_INK = 0.45


def fetch_nls_tile(layer_path, zoom, x, y, cache):
    path = cache / layer_path / str(zoom) / str(x) / f"{y}.png"
    if path.exists():
        return path if path.stat().st_size else None
    path.parent.mkdir(parents=True, exist_ok=True)
    request = urllib.request.Request(f"{NLS_URL}/{layer_path}/{zoom}/{x}/{y}.png", headers={"User-Agent": USER_AGENT})
    for attempt in range(3):
        try:
            with urllib.request.urlopen(request, timeout=30) as response:
                path.write_bytes(response.read())
                return path
        except urllib.error.HTTPError as error:
            if error.code in (403, 404):
                path.write_bytes(b"")
                return None
            time.sleep(2 ** attempt)
        except urllib.error.URLError:
            time.sleep(2 ** attempt)
    raise RuntimeError(f"failed to fetch {layer_path}/{zoom}/{x}/{y}")


def nls_layer_bounds(layer_path, cache):
    path = cache / layer_path / "metadata.json"
    if not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        request = urllib.request.Request(f"{NLS_URL}/{layer_path}/metadata.json", headers={"User-Agent": USER_AGENT})
        with urllib.request.urlopen(request, timeout=30) as response:
            path.write_bytes(response.read())
    bounds = json.loads(path.read_text())["bounds"]
    if isinstance(bounds, str):
        bounds = bounds.split(",")
    return [float(value) for value in bounds]


def mercator_pixels(longitude, latitude, zoom):
    scale = NLS_TILE * 2 ** zoom
    x = (longitude + 180) / 360 * scale
    y = (1 - numpy.arcsinh(numpy.tan(numpy.radians(latitude))) / math.pi) / 2 * scale
    return x, y


class NlsSource:
    def __init__(self, layer, cache, supersample=2, west=None, south=None, east=None, north=None):
        self.layer = layer
        self.cache = Path(cache)
        self.supersample = supersample
        self.millimetres_per_pixel = round(NLS_LAYERS[layer].metres_per_pixel * 1000)
        self.extent = (west, south, east, north)

    def bounds(self):
        return self.extent

    def within(self, west, south, east, north):
        return self

    def render(self, level, column, row):
        layer = NLS_LAYERS[self.layer]
        size = gmap.TILE * self.supersample
        metres_per_pixel = level.metres_per_pixel / self.supersample
        rgba = self.render_rgba(layer, column * level.span, (row + 1) * level.span, size, metres_per_pixel)
        if rgba is None:
            return None
        if layer.cleaned:
            return clean_ink(rgba, self.supersample)
        return average_down(rgba, self.supersample)

    def render_rgba(self, layer, west, north, size, metres_per_pixel):
        offsets = (numpy.arange(size) + 0.5) * metres_per_pixel
        eastings, northings = numpy.meshgrid(west + offsets, north - offsets)
        longitudes, latitudes = bng_to_wgs84.transform(eastings, northings)
        pixel_x, pixel_y = mercator_pixels(longitudes, latitudes, layer.zoom)
        tile_x0 = int(pixel_x.min()) // NLS_TILE
        tile_y0 = int(pixel_y.min()) // NLS_TILE
        tile_x1 = int(pixel_x.max()) // NLS_TILE + 1
        tile_y1 = int(pixel_y.max()) // NLS_TILE + 1

        requests = []
        for layer_path in layer.paths:
            layer_west, layer_south, layer_east, layer_north = nls_layer_bounds(layer_path, self.cache)
            if layer_west < longitudes.max() and layer_east > longitudes.min() and layer_south < latitudes.max() and layer_north > latitudes.min():
                requests += [(layer_path, x, y) for x in range(tile_x0, tile_x1 + 1) for y in range(tile_y0, tile_y1 + 1)]
        with ThreadPoolExecutor(NLS_FETCH_THREADS) as executor:
            paths = list(executor.map(lambda request: fetch_nls_tile(request[0], layer.zoom, request[1], request[2], self.cache), requests))

        mosaic = numpy.zeros(((tile_y1 - tile_y0 + 1) * NLS_TILE, (tile_x1 - tile_x0 + 1) * NLS_TILE, 4), numpy.float32)
        found = False
        for (_, x, y), path in zip(requests, paths):
            if path is None:
                continue
            tile = numpy.asarray(Image.open(path).convert("RGBA"), numpy.float32)
            covered = tile[..., 3:] / 255
            region = mosaic[(y - tile_y0) * NLS_TILE:(y - tile_y0 + 1) * NLS_TILE, (x - tile_x0) * NLS_TILE:(x - tile_x0 + 1) * NLS_TILE]
            region[..., :3] = region[..., :3] * (1 - covered) + tile[..., :3] * covered
            region[..., 3:] = numpy.maximum(region[..., 3:], covered)
            found = True
        if not found:
            return None

        local_x = pixel_x - tile_x0 * NLS_TILE - 0.5
        local_y = pixel_y - tile_y0 * NLS_TILE - 0.5
        x0 = numpy.clip(numpy.floor(local_x).astype(int), 0, mosaic.shape[1] - 2)
        y0 = numpy.clip(numpy.floor(local_y).astype(int), 0, mosaic.shape[0] - 2)
        fraction_x = numpy.clip(local_x - x0, 0, 1)[..., None]
        fraction_y = numpy.clip(local_y - y0, 0, 1)[..., None]
        top = mosaic[y0, x0] * (1 - fraction_x) + mosaic[y0, x0 + 1] * fraction_x
        bottom = mosaic[y0 + 1, x0] * (1 - fraction_x) + mosaic[y0 + 1, x0 + 1] * fraction_x
        return top * (1 - fraction_y) + bottom * fraction_y


def clean_ink(rgba, supersample):
    luminance = rgba[..., 0] * 0.299 + rgba[..., 1] * 0.587 + rgba[..., 2] * 0.114
    paper_level = numpy.percentile(luminance, 75)
    ink_level = 60.0
    ink = numpy.clip((paper_level - luminance) / max(paper_level - ink_level, 1), 0, 1)
    size = ink.shape[0] // supersample
    ink = ink.reshape(size, supersample, size, supersample).mean(axis=(1, 3))
    covered = rgba[..., 3].reshape(size, supersample, size, supersample).mean(axis=(1, 3)) >= 0.5

    ink[ink < FAINT_INK] = 0
    padded = numpy.pad(ink > 0, 1)
    neighbours = sum(padded[1 + dy:1 + dy + size, 1 + dx:1 + dx + size] for dy in (-1, 0, 1) for dx in (-1, 0, 1) if dy or dx)
    ink[(neighbours == 0) & (ink < LONELY_INK)] = 0

    steps = numpy.round(ink * (INK_LEVELS - 1)) / (INK_LEVELS - 1)
    colour = PAPER[None, None, :] * (1 - steps[..., None]) + INK[None, None, :] * steps[..., None]
    pixels = numpy.zeros((size, size, 4), numpy.uint8)
    pixels[..., :3] = numpy.round(colour).astype(numpy.uint8)
    pixels[..., 3] = numpy.where(covered, 255, 0)
    return pixels if covered.any() else None


def average_down(rgba, supersample):
    size = rgba.shape[0] // supersample
    averaged = rgba.reshape(size, supersample, size, supersample, 4).mean(axis=(1, 3))
    pixels = numpy.zeros((size, size, 4), numpy.uint8)
    pixels[..., :3] = numpy.round(averaged[..., :3]).astype(numpy.uint8)
    pixels[..., 3] = numpy.where(averaged[..., 3] >= 0.5, 255, 0)
    return pixels if pixels[..., 3].any() else None
