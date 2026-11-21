import re

from pyproj import Transformer

PROJECTION = "EPSG:27700"

wgs84_to_bng = Transformer.from_crs("EPSG:4326", PROJECTION, always_xy=True)
bng_to_wgs84 = Transformer.from_crs(PROJECTION, "EPSG:4326", always_xy=True)

GRID_LETTERS = "ABCDEFGHJKLMNOPQRSTUVWXYZ"
QUADRANTS = {"SW": (0, 0), "SE": (1, 0), "NW": (0, 1), "NE": (1, 1)}
QUARTER_NAME = re.compile(r"^([A-Z]{2})(\d)(\d)(NE|NW|SE|SW)$")
SQUARE_NAME = re.compile(r"^([A-Z]{2})$")
SQUARE_SIZE = 100000
QUARTER_SIZE = 5000


def square_origin(letters):
    first = GRID_LETTERS.index(letters[0])
    second = GRID_LETTERS.index(letters[1])
    easting = ((first - 2) % 5) * 5 + (second % 5)
    northing = (19 - (first // 5) * 5) - (second // 5)
    return easting * SQUARE_SIZE, northing * SQUARE_SIZE


def square_letters(easting, northing):
    east_square = int(easting // SQUARE_SIZE)
    north_square = int(northing // SQUARE_SIZE)
    first = (19 - north_square) - (19 - north_square) % 5 + (east_square + 10) // 5
    second = (19 - north_square) * 5 % 25 + east_square % 5
    return GRID_LETTERS[first] + GRID_LETTERS[second]


def squares_touching(west, south, east, north):
    letters = []
    for northing in range(int(south // SQUARE_SIZE) * SQUARE_SIZE, int(north), SQUARE_SIZE):
        for easting in range(int(west // SQUARE_SIZE) * SQUARE_SIZE, int(east), SQUARE_SIZE):
            letters.append(square_letters(easting, northing))
    return letters


def named_bounds(name):
    match = QUARTER_NAME.match(name)
    if match:
        letters, east_digit, north_digit, quadrant = match.groups()
        origin_east, origin_north = square_origin(letters)
        quadrant_east, quadrant_north = QUADRANTS[quadrant]
        west = origin_east + int(east_digit) * 10000 + quadrant_east * QUARTER_SIZE
        south = origin_north + int(north_digit) * 10000 + quadrant_north * QUARTER_SIZE
        return west, south, west + QUARTER_SIZE, south + QUARTER_SIZE
    if SQUARE_NAME.match(name):
        west, south = square_origin(name)
        return west, south, west + SQUARE_SIZE, south + SQUARE_SIZE
    return None


def lonlat_bounds(west, south, east, north, margin=0.0):
    corners = [bng_to_wgs84.transform(x, y) for x in (west, east) for y in (south, north)]
    longitudes = [longitude for longitude, _ in corners]
    latitudes = [latitude for _, latitude in corners]
    return min(longitudes) - margin, min(latitudes) - margin, max(longitudes) + margin, max(latitudes) + margin
