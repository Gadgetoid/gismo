# Developing GISmo

## Layout

```
src/core            portable C++, host tested: NMEA, grid, map packs, missions, routes, places, JPEG, zip, drawing, settings
src/platform        Windows CE helpers: GPS port, log, BMP files
src/app             the app: screens, input, sessions, exploring, outings; app.h holds shared state and layout
src/devices/junosb  Juno SB specifics: buttons, GPS port, backlight, NullCam camera driver
src/diagnostics     KeyTest and CamTest
planner             the mission planner and outing viewer: gismo.js (file formats), mapview.js (the map), gismo.css
tools               map, route, places, hint and icon tools (Python, run with uv)
areas, maps, missions   the Cambridge example: area file, built maps, mission
local               your own areas and missions, not committed (local/missions/*.txt go on the card)
tests               host, planner, browser and screenshot tests
docs                user documentation; camera-driver.md has the NullCam protocol
```

## Building

Needs [grimace](https://github.com/grimace-sdk/grimace) (`GRIMACE=`, default `../../grimace`) and its [llvm-builds](https://github.com/grimace-sdk/llvm-builds) LLVM (`GRIMACE_ARM_LLVM=`), plus uv and Node for the tools and tests.

```
make                 build/junosb/GISmo.exe, then grimace's import check
make packages        build/junosb/packages: CAB, self-installer and zip
make card            build/card/GISmo, ready to copy to a card (DIAGNOSTICS=no leaves out KeyTest and CamTest)
make maps            every local/areas/*.toml into build/maps/<area>/
make example-maps    rebuild maps/ from areas/cambridge.toml
make site            build/site: the planner and viewer with the example maps, as on GitHub Pages
make test            host and planner tests
make browser-test    planner and viewer checks in Chrome (PLAYWRIGHT_CHANNEL= uses Playwright's Chromium)
make screens         every screen, drawn by the app's own code on the host: build/screens
make docs-screenshots    refresh docs/screenshots
```

GPS is NMEA on COM4 at 4800 baud. `replay.nmea` beside the exe replaces it; `tools/make_nmea.py` writes one.

## Tests

- `tests/host`: the core in C++ with ASan and UBSan, checked against Python references: pyproj for the grid, the Python map reader, Pillow for JPEG quality (relative to its own 4:2:0 output) and EXIF, a Python Dijkstra for routes, `zipfile` for zips.
- `tests/planner/test.js`: `gismo.js` in Node, against the same references and the device's mission rules.
- `tests/planner/browser_test.py`: the planner and viewer in Chromium, with OpenStreetMap tiles intercepted.
- `tests/screens`: builds `src/app` for the host against a small stand-in for the Windows CE calls it uses (`shim.cpp`: GDI fills and text with Tahoma through `stb_truetype.h`, files, `wsprintfW`), walks a scripted outing through the Cambridge example, checks the camera isn't interrupted and the outing zip is complete, and saves every screen. Set `GISMO_FONTS`, or `GISMO_FONT` and `GISMO_BOLD_FONT`, where Tahoma isn't in `/System/Library/Fonts/Supplemental`.

## Continuous integration

`.github/workflows/build.yml`:

- `test`: all the tests above, with DejaVu Sans for the screenshots.
- `device`: builds with the latest grimace and llvm-builds releases (or a tag or branch when run by hand), with no warnings allowed, and uploads the CAB, installer, zip, card zip and planner zip.
- `site` and `pages`: publish the planner and viewer on `main`.
- `release`: on a `v*` tag, publishes the files as a GitHub release.

## Another device

Add `src/devices/<name>` with a `device_profile` (buttons, GPS port and speed, backlight) and a `camera` implementation, and build with `-DGISMO_DEVICE=<name>`. Screen sizes are in `layout` in `app.h`.

On the Juno SB, measured with KeyTest (which writes `KeyTest.txt`):

| Button | Codes |
|---|---|
| Rocker, pad up and down | 26, 28 (indistinguishable) |
| Pad left, right | 25, 27 |
| Pad centre | 86 + 0D |
| Top-left minus, top-right minus | 5B + C1, 5B + C2 |
| Camera, Home, Mail | 5B + C3, 5B + C4, 5B + C5 |
| Windows | 5B alone |
| OK | 5C + 75 |

GISmo calls `AllKeys(TRUE)` while active so the shell doesn't take them.

## Icons

`tools/make_icons.py` renders Material Symbols to alpha masks in `src/app/icons.h`, and `tools/make_app_icon.py` writes `assets/gismo.ico` with 32-bit bitmap entries (Windows CE can't read PNG ones). Both take the variable font and its `.codepoints` file from google/material-design-icons (`variablefont/`).

## File formats

All little endian.

### Map packs (`.gmap`)

Header, 32 bytes:

| Offset | Type | Field |
|---|---|---|
| 0 | char[8] | `GISMOMAP` |
| 8 | u16 | version, 1 |
| 10 | u16 | tile size, 256 |
| 12 | u16 | level count |
| 14 | u16 | flags, 0 |
| 16 | u32 | level table offset |
| 20 | u32 | text offset |
| 24 | u32 | text length |

Text is UTF-8 `key=value` lines: `name`, `attribution`, `projection` (`EPSG:27700`).

Level table, 32 bytes per level, finest first, each half the resolution of the one before:

| Offset | Type | Field |
|---|---|---|
| 0 | u32 | millimetres per pixel |
| 4 | i32 | west edge, BNG metres |
| 8 | i32 | north edge, BNG metres |
| 12 | u32 | columns |
| 16 | u32 | rows |
| 20 | u32 | index offset |

Tiles cover `256 * metres per pixel` metres on a grid anchored at BNG 0,0, so packs with the same resolution share tile edges. The index is `columns * rows` entries of `u32 offset, u32 length`, top row first; a length of 0 means no tile.

Tile:

| Offset | Type | Field |
|---|---|---|
| 0 | u8 | flags: bit 0, palette entry 0 is transparent |
| 1 | u8 | colour count - 1 |
| 2 | u16[count] | palette, RGB565 |
| 2 + 2 * count | | zlib stream of 256 x 256 palette indices, top row first; absent if count is 1 |

Transparent pixels are outside the source maps; draw a coarser level or another pack underneath.

### Walking routes (`.groute`)

Header `GISMORTE`, u16 version 1, u16 0, u32 node count, u32 edge count; then nodes (i32 east, i32 north, in decimetres), node count + 1 u32 edge offsets, and edges (u32 target, u32 cost). Costs are decimetres times the way's walking factor (`tools/make_routes.py`). Tiles are named `<area>-<west km>-<south km>-<size km>.groute`; GISmo loads the one whose core holds the target.

### Places (`.gplaces`)

| Offset | Type | Field |
|---|---|---|
| 0 | char[8] | `GISMOPLC` |
| 8 | u16 | version, 1 |
| 10 | u16 | kind count |
| 12 | u32 | place count |
| 16 | i32, i32 | grid west, south (BNG metres) |
| 24 | u32 | cell size (metres) |
| 28 | u32, u32 | columns, rows |
| 36 | u32 | kinds offset: per kind, u8 category (0 play, 1 history, 2 nature, 3 sights), u8 length, UTF-8 label; padded to 4 bytes |
| 40 | u32 | cells offset: columns * rows + 1 u32 start indices, bottom row first |
| 44 | u32 | places offset: per place, i32 east, i32 north, u16 kind, u16 0, u32 name offset (0 is unnamed) |
| 48 | u32, u32 | names offset and length: NUL-terminated UTF-8 |

### Outing zips

Stored (uncompressed) zips with UTF-8 names, under one folder named like the zip: `summary.txt`, `mission.txt`, `progress.txt`, `events.csv`, `tracks/*.gpx` and `photos/*.jpg`.
