import struct
import zlib

import numpy
from PIL import Image

MAGIC = b"GISMOMAP"
VERSION = 1
TILE = 256
HEADER = struct.Struct("<8sHHHHIII4x")
LEVEL = struct.Struct("<IiiIII8x")
INDEX_ENTRY = struct.Struct("<II")
FLAG_TRANSPARENT = 1


class Level:
    def __init__(self, millimetres_per_pixel, first_column, first_row, columns, rows):
        self.millimetres_per_pixel = millimetres_per_pixel
        self.first_column = first_column
        self.first_row = first_row
        self.columns = columns
        self.rows = rows
        self.index = {}

    @property
    def metres_per_pixel(self):
        return self.millimetres_per_pixel / 1000

    @property
    def span(self):
        return self.millimetres_per_pixel * TILE // 1000

    @property
    def origin_east(self):
        return self.first_column * self.span

    @property
    def origin_north(self):
        return (self.first_row + self.rows) * self.span

    def tiles(self):
        for row in range(self.first_row, self.first_row + self.rows):
            for column in range(self.first_column, self.first_column + self.columns):
                yield column, row


def rgb565(pixels):
    red = pixels[..., 0].astype(numpy.uint16)
    green = pixels[..., 1].astype(numpy.uint16)
    blue = pixels[..., 2].astype(numpy.uint16)
    return (red >> 3) << 11 | (green >> 2) << 5 | blue >> 3


def rgb_from_565(values):
    values = values.astype(numpy.uint16)
    red = (values >> 11) & 0x1F
    green = (values >> 5) & 0x3F
    blue = values & 0x1F
    return numpy.stack([(red << 3) | (red >> 2), (green << 2) | (green >> 4), (blue << 3) | (blue >> 2)], axis=-1).astype(numpy.uint8)


def encode_tile(pixels):
    covered = pixels[..., 3] >= 128
    if not covered.any():
        return None
    transparent = not covered.all()
    limit = 255 if transparent else 256

    rgb = pixels[..., :3].copy()
    rgb[~covered] = rgb[covered][0]
    colours = rgb565(rgb)
    unique, inverse = numpy.unique(colours, return_inverse=True)
    if len(unique) > limit:
        quantized = Image.fromarray(rgb, "RGB").quantize(limit, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
        colours = rgb565(numpy.asarray(quantized.convert("RGB")))
        unique, inverse = numpy.unique(colours, return_inverse=True)

    indices = inverse.reshape(colours.shape).astype(numpy.uint8)
    palette = unique.astype("<u2")
    if transparent:
        indices += 1
        indices[~covered] = 0
        palette = numpy.concatenate([numpy.zeros(1, "<u2"), palette])

    data = struct.pack("<BB", FLAG_TRANSPARENT if transparent else 0, len(palette) - 1) + palette.tobytes()
    if len(palette) > 1:
        data += zlib.compress(indices.tobytes(), 9)
    return data


def decode_tile(data):
    flags, count_minus_one = struct.unpack_from("<BB", data)
    count = count_minus_one + 1
    palette = numpy.frombuffer(data, "<u2", count, 2)
    if count > 1:
        indices = numpy.frombuffer(zlib.decompress(data[2 + count * 2:]), numpy.uint8).reshape(TILE, TILE)
    else:
        indices = numpy.zeros((TILE, TILE), numpy.uint8)
    pixels = numpy.empty((TILE, TILE, 4), numpy.uint8)
    pixels[..., :3] = rgb_from_565(palette)[indices]
    pixels[..., 3] = 255
    if flags & FLAG_TRANSPARENT:
        pixels[indices == 0] = 0
    return pixels


class Writer:
    def __init__(self, path, text):
        self.path = path
        self.text = text
        self.levels = []
        self.file = open(path, "wb")
        self.file.write(bytes(HEADER.size))

    def add_level(self, level):
        self.levels.append(level)

    def add_tile(self, level, column, row, data):
        offset = self.file.tell()
        self.file.write(data)
        level.index[(column, row)] = (offset, len(data))

    def flush(self):
        self.file.flush()

    def close(self):
        index_offsets = []
        for level in self.levels:
            index_offsets.append(self.file.tell())
            for column, row in self.top_down(level):
                self.file.write(INDEX_ENTRY.pack(*level.index.get((column, row), (0, 0))))

        level_table_offset = self.file.tell()
        for level, index_offset in zip(self.levels, index_offsets):
            self.file.write(LEVEL.pack(level.millimetres_per_pixel, level.origin_east, level.origin_north, level.columns, level.rows, index_offset))

        text = self.text.encode("utf-8")
        text_offset = self.file.tell()
        self.file.write(text)

        self.file.seek(0)
        self.file.write(HEADER.pack(MAGIC, VERSION, TILE, len(self.levels), 0, level_table_offset, text_offset, len(text)))
        self.file.close()

    @staticmethod
    def top_down(level):
        for row in reversed(range(level.first_row, level.first_row + level.rows)):
            for column in range(level.first_column, level.first_column + level.columns):
                yield column, row


class Reader:
    def __init__(self, path):
        self.file = open(path, "rb")
        magic, version, tile, level_count, _, level_table_offset, text_offset, text_length = HEADER.unpack(self.file.read(HEADER.size))
        if magic != MAGIC or version != VERSION or tile != TILE:
            raise ValueError(f"{path}: not a GISmo map")
        self.file.seek(text_offset)
        self.text = self.file.read(text_length).decode("utf-8")
        self.levels = []
        self.file.seek(level_table_offset)
        records = [LEVEL.unpack(self.file.read(LEVEL.size)) for _ in range(level_count)]
        for millimetres_per_pixel, origin_east, origin_north, columns, rows, index_offset in records:
            span = millimetres_per_pixel * TILE // 1000
            level = Level(millimetres_per_pixel, origin_east // span, origin_north // span - rows, columns, rows)
            self.file.seek(index_offset)
            entries = self.file.read(columns * rows * INDEX_ENTRY.size)
            for position, (column, row) in enumerate(Writer.top_down(level)):
                offset, length = INDEX_ENTRY.unpack_from(entries, position * INDEX_ENTRY.size)
                if length:
                    level.index[(column, row)] = (offset, length)
            self.levels.append(level)

    def read(self, level, column, row):
        entry = level.index.get((column, row))
        if entry is None:
            return None
        self.file.seek(entry[0])
        return self.file.read(entry[1])

    def tile_pixels(self, level, column, row):
        data = self.read(level, column, row)
        return None if data is None else decode_tile(data)

    def render(self, level, west, north, width, height):
        image = numpy.zeros((height, width, 4), numpy.uint8)
        metres_per_pixel = level.metres_per_pixel
        east = west + width * metres_per_pixel
        south = north - height * metres_per_pixel
        span = level.span
        for row in range(int(south // span), int(-(-north // span))):
            for column in range(int(west // span), int(-(-east // span))):
                pixels = self.tile_pixels(level, column, row)
                if pixels is None:
                    continue
                x = round((column * span - west) / metres_per_pixel)
                y = round((north - (row + 1) * span) / metres_per_pixel)
                paste(image, pixels, x, y)
        return image


def paste(target, source, x, y):
    target_height, target_width = target.shape[:2]
    source_height, source_width = source.shape[:2]
    left, top = max(x, 0), max(y, 0)
    right, bottom = min(x + source_width, target_width), min(y + source_height, target_height)
    if left >= right or top >= bottom:
        return
    patch = source[top - y:bottom - y, left - x:right - x]
    covered = patch[..., 3] >= 128
    region = target[top:bottom, left:right]
    region[covered] = patch[covered]
