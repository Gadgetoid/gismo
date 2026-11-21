# GISmo

A kids' GPS datalogger for the Trimble Juno SB: walk to waypoints, complete tasks, take geotagged photos, replay the trip on a PC.

## Layout

```
src/core            portable C++, host tested: NMEA, grid, map packs, missions, routes, JPEG, drawing, settings
src/platform        Windows CE helpers: GPS port, log, BMP files
src/app             the app: screens, input, session logging; app.h holds shared state and layout
src/devices/junosb  Juno SB specifics: buttons, GPS port, backlight, NullCam camera driver
src/diagnostics     KeyTest and CamTest
missions            example missions
local               your own missions, not committed (local/missions/*.txt go on the card)
planner             the mission planner web page
tools               map pack, route, places, hint and icon tools (Python, run with uv)
tests               host and planner tests
```

A new device needs a `src/devices/<name>` with a `device_profile` and a `camera` implementation, built with `-DGISMO_DEVICE=<name>`.

## Installing

Each release has:

- `GISmo-<version>-junosb.cab`: copy to the Juno and tap it to install GISmo to `\Program Files\GISmo`, with a Start menu shortcut. `-setup.exe` does the same without wceload; `-junosb.zip` has the same files.
- `GISmo-<version>-card.zip`: copy its `GISmo` folder to the storage card. It has the example Cambridge map and mission, the camera filters, and the planner and viewer. GISmo finds `\<card>\GISmo\Maps`, `Missions` and `Filters` wherever it's installed, and keeps `Sessions` and `Outings` beside them. The card folder can also hold `GISmo.exe` itself and be run from there.
- `GISmo-<version>-planner.zip`: the planner and viewer to open from a PC. They're also on GitHub Pages, where they open with the example map.

## Continuous integration

`.github/workflows/build.yml` runs on every push and pull request:

- `test`: host, planner and browser tests, and the screenshots (with DejaVu Sans standing in for Tahoma), uploaded as an artifact.
- `device`: builds GISmo with the latest grimace release and llvm-builds release (or a tag or branch given when run by hand), checks its imports, packages it, and uploads the CAB, installer, zip, card zip and planner zip.
- `pages`: on `main`, publishes the planner and viewer, with the example map, to GitHub Pages.
- `release`: on a `v*` tag, publishes those files as a GitHub release.

## Building

Needs [grimace](https://github.com/grimace-sdk/grimace) (set `GRIMACE=`, default `../../grimace`).

```
make            build/junosb/GISmo.exe
make maps       build every local/areas/*.toml into build/maps/<area>/ (AREAS= to choose)
make card       build/card/GISmo: exe, maps (maps/ and build/maps/), missions (missions/ and local/missions/); DIAGNOSTICS=no leaves out KeyTest and CamTest
make packages   build/junosb/packages: the CAB, self-installer and zip (grimace_package)
make site       build/site: the planner and viewer with the example maps, as published to GitHub Pages
make test       host and planner tests, against the example maps in maps/
make example-maps  rebuild maps/ from areas/cambridge.toml
make screens    screenshots of every screen without a device: build/screens/*.png and contact-sheet.png
make browser-test  planner and viewer checks in Chrome (Playwright; PLAYWRIGHT_CHANNEL= uses its own Chromium)
```

Copy `build/card/GISmo` to `\Storage Card\GISmo`. GPS is NMEA on COM4 at 4800 baud. `GISmo.log` next to the exe logs startup and GPS steps.

On the card:

```
GISmo.exe
Maps/*.gmap         every pack is loaded; packs at 10 m/px or coarser are the overview, drawn under the detailed layer; "Map" switches detailed layers
Maps/*.gplaces      places worth visiting, for exploring without a mission
Maps/*.groute       walking routes
Missions/*.txt      choose one from the Mission page (folder icon); the last one chosen is remembered
Sessions/<mission>/ one folder per mission (named after its file; "Free roam" without one):
    progress.txt        completed waypoints, times and answers; reloaded when the mission is chosen
    events.csv          appended on every outing
    tracks/<UTC time>Z.gpx  one track per outing, valid after every point; a point every 5 m (or the fix's error, if larger) or 15 s
    photos/
Sessions/last-mission.txt
Outings/<mission>-<UTC time>Z.zip   saved when a mission is finished, or with Save outing: summary.txt, the tracks, events, progress and photos
```

Delete a waypoint's section in `progress.txt`, or the whole file, to do it again.

The walking figure in the Mission page header (or right on the pad) shows this outing so far: distance, time, average speed, stops found and photos. Save outing writes the zip in `Outings`; finishing a mission does it automatically. Saving starts a new track file, so a mid-walk save leaves the walk in two GPX files, both included in later zips.

## Missions

Plain text, one `[section]` per waypoint, visited in order. See `missions/cambridge.txt`.

```
title = Cambridge Explorer
intro = A walk through the middle of Cambridge.

[Corpus Clock]
location = 52.203759, 0.117688    latitude, longitude (WGS84)
radius = 25                        metres; optional, 25 by default
task = choice                      visit, photo, choice or count
clue = Look for a gold clock on the corner of a building.
prompt = What creature sits on top of the clock?
choices = Grasshopper | Dragon | Owl | Cat
answer = Grasshopper
```

`task = via` makes a route point: a place on the way, to guide a walk along a particular path. Its name is optional (`[]`), it has no clue or task, and arriving just ticks it off with a beep and points on to the next waypoint. Route points show as small dots and aren't counted as stops.

`route = yes` on any waypoint shows the walking route to it on the map while it's the target, without pressing the route button. The planner previews these legs along the paths when the area's `.groute` files are loaded.

`\n` in a value is a line break. Lines starting with `#` are comments.

A waypoint can have an optional photo hint, `photo = church.bmp`, a 320x240 16-bit BMP in the same folder as the mission. The clue card then offers a "Photo hint" button (or left/right). `tools/make_hint.py` turns any photos into hints (crops to 4:3, resizes); the `photos/view/*.bmp` GISmo saves for its own photos work as they are, so a grown-up can walk a mission first and reuse those.

```
uv run tools/make_hint.py church.jpg playground.heic --output missions
```

## Exploring

Without a mission ("No mission" in the picker), the Mission page lists the nearest places worth visiting, from every `Maps/*.gplaces`: playgrounds and parks; monuments, ruins, mills and churches; viewpoints, hill tops and ponds; museums, artwork and fountains. Left/right (or the tabs) switch between All, Play, History, Nature and Sights. Choosing one makes it the target: the map shows the arrow, distance, warmer/colder and walking route as for a mission waypoint, and arriving shows "You found it!", ticks it off and logs a `visited` event. Without a GPS fix the list is sorted from the middle of the map.

`tools/make_places.py` (run by `build_area.py`) writes `<area>.gplaces`, a curated subset of `<area>.places.json`. Little endian:

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

## Keys

Measured with `KeyTest.exe` (writes `KeyTest.txt`). The side rocker and the 5-way pad's up/down send identical codes (vk 26/28, scan 6C/6A), so they can't be told apart. App buttons arrive as Windows (5B) plus the button; GISmo calls `AllKeys(TRUE)` while active so the shell doesn't take them.

| Button | Code | GISmo |
|---|---|---|
| Rocker / pad up, down | 26, 28 | zoom, or pan north/south in pan mode; move selection on lists and task cards; camera: zoom |
| Pad left, right | 25, 27 | pan |
| Pad centre | 86 + 0D | confirm; on the map, re-centre on you |
| Top-left minus | 5B + C1 | previous page (Map, Mission, Sky) |
| Top-right minus | 5B + C2 | next page |
| Camera | 5B + C3 | confirm (photo, once the camera is in) |
| Home | 5B + C4 | re-centre on you |
| Mail | 5B + C5 | map layer |
| Windows | 5B alone | pan mode: up/down pan too; switches itself off 3 s after the last pan press |
| OK | 5C + 75 | confirm |

With `log_input = yes` in `GISmo.ini`, key presses (first 400) and drags are logged to `GISmo.log`.

## Settings

An optional `GISmo.ini` next to the exe overrides the defaults. Unknown keys and out of range values are ignored.

```
track_spacing_metres = 5        log a track point after moving this far...
track_interval_seconds = 15     ...or after this long
warmth_interval_seconds = 8     how often "warmer/colder" is judged
warmth_threshold_metres = 4     how much closer or further counts
route_refresh_metres = 20       re-plan the walking route after moving this far
clock_wait_seconds = 120        how long to wait for GPS time before starting a session on the device clock
pan_mode_timeout_seconds = 3
metres_per_step = 0.6           for the "about N steps" distance
max_drag_jump_pixels = 80       touch samples jumping further than this are ignored
jpeg_quality = 85
log_input = no
```

## Mission planner

`planner/index.html` is an offline page for building missions on a PC (`make card` also puts it in `Planner/` on the card). In Chrome or Edge, **Open card** picks the card (or its `GISmo` folder): it loads every map pack in `Maps`, the OpenStreetMap places file, and the missions in `Missions`, and **Save** writes the mission `.txt` and any photo hints straight back. In other browsers, load map packs and places with **Maps and places…** and a mission with **Mission file…**; **Save** then downloads the files.

- Click the map to add a waypoint, or click a place (or pick one from the Places list); click the line between two waypoints to insert one there. Drag markers to move them, and drag waypoint names in the side panel to reorder. Scroll, pinch or use +/- to zoom; the layer menu switches between modern and old maps.
- Keys: Delete (or Backspace) removes the selected waypoint, Ctrl+Z / Cmd+Z undoes, Ctrl+Shift+Z or Ctrl+Y redoes, Ctrl+S saves, Escape deselects. While typing in a field they do their usual text editing.
- **Export zip** saves the mission and its photo hints as a zip (unzip into the card's `GISmo` folder). With a card open, **Back up card** zips its `Missions` and `Sessions` folders. In Chrome and Edge both ask where to save.
- Each waypoint's name, task, arrival radius, clue, prompt, choices and right answer are edited in its card; the arrival radius shows on the map.
- Choosing a photo hint crops and converts it to the 320x240 BMP the Juno shows.
- The mission file updates live under **Mission file**, and problems that would stop it loading on the Juno are listed before saving.

`tools/make_places.py` (run by `make maps`) exports named OpenStreetMap places, plus a few unnamed kinds that make good targets (playgrounds, post boxes, viewpoints...), to `Maps/<area>.places.json`. The page's shared logic is `planner/gismo.js`; `make -C tests/planner` checks it in Node against pyproj, the device's mission rules, the Python map reader and `make_hint.py`.

## Outing viewer

`planner/viewer.html` (also in `Planner/` on the card) shows a finished outing: open an outing `.zip` from the card's `Outings` folder, or a mission's files from `Sessions` (Chrome and Edge can open the folder itself). It draws the track, the mission's stops (green when done) and each photo where it was taken, from the photo's own GPS tags, and lists what happened, with the photos, in order. Click a photo, on the map or in the list, to see it full size. Add the area's `.gmap` files with **Maps...** to see the map underneath; wherever there isn't one, the viewer shows OpenStreetMap's standard tiles instead (untick **Show OpenStreetMap** to stay offline). The tiles are Web Mercator, placed on the British National Grid by their corners. They come from tile.openstreetmap.org, which is for light use and needs a Referer, so it works best from a web server such as GitHub Pages rather than a `file://` page. The planner doesn't use them. The viewer and planner share `gismo.js` (file formats), `mapview.js` (the map) and `gismo.css`.

## Walking routes

`make maps` also builds `Maps/<area>.groute`, a walking network from the same OpenStreetMap extract: paths, footways, cycleways, bridleways, steps and tracks at their length; residential streets x1.3, service roads (drives, car parks) x1.8, other roads x1.5 to x2; urban trunk roads (40 mph or less) x3; roads at 40 mph or more a further x1.5; motorways and faster trunk roads left out (`tools/make_routes.py`). Routes aren't drawn when either end is more than 150 m from a path or road. On the map, the route button (above re-centre, shown while there's a target) loads it on first use and draws an A* route from you to the next waypoint, refreshed as the target changes or you move 20 m (`route_refresh_metres`); with several areas or route tiles on the card it loads the one covering the target (tiles are named `<area>-<west km>-<south km>-<size km>.groute`); the target strip then shows the walking distance. `tests/host/check_routes.py` checks the A* costs against a Python Dijkstra.

## Screenshots

`make screens` builds the app's screens for macOS or Linux against a small stand-in for the Windows CE calls they use (`tests/screens/shim.cpp`: GDI fills and text, files, `wsprintfW`), then walks a scripted outing through the Cambridge example (`tests/screens/screens.cpp`) and saves each screen, drawn by the app's own code, as a PNG. Text uses Tahoma (on macOS at `/System/Library/Fonts/Supplemental`; set `GISMO_FONTS` to a folder with `Tahoma.ttf` and `Tahoma Bold.ttf` elsewhere) through `stb_truetype.h` (public domain). Camera frames are drawn landscapes, so no photos are needed.

## Icons

`tools/make_icons.py` renders Google's Material Symbols (Apache 2.0) to alpha masks in `src/app/icons.h`, and `tools/make_app_icon.py` writes `assets/gismo.ico` with 32-bit bitmap entries (Windows CE can't read PNG ones). Both take the variable font and its `.codepoints` file from github.com/google/material-design-icons (`variablefont/`).

For testing indoors, put an NMEA file named `replay.nmea` next to the exe: GISmo plays it back, one fix per second, instead of reading COM4, and loops. `tools/make_nmea.py` writes a walk in a circle (`--centre EAST NORTH`, `--radius`, `--speed`) with 20 seconds of satellite acquisition first; `make card` puts one in the card folder as `replay.nmea.example`.

## Map packs

Map packs (`.gmap`) are seamless raster maps on the British National Grid, one file per layer. An area file describes everything for one place:

```
name = "Cambridge"
bbox = [543000, 256000, 547000, 260000]          BNG metres: west, south, east, north
osm = "europe/united-kingdom/england/cambridgeshire"   Geofabrik extract, or a list of them: footpaths, routes, places
route_tile = 10000        optional: split walking routes into 10 km tiles (2.5 km overlap) to keep each graph small

[[maps]]
source = "os-local"       OS OpenMap - Local, 2 m/px
paths = true              draw OpenStreetMap footpaths (red-orange dashes) and tracks (brown dashes)

[[maps]]
suffix = "1900"
source = "nls"            National Library of Scotland historic OS maps
layer = "6inch"           6inch, 6inch1880, 1inch1890, 1inch1900, 1inch1940

[[maps]]
suffix = "overview"
source = "os-overview"    OS 1:250 000 and MiniScale
margin = 40000            metres beyond bbox
```

```
cd tools
uv run build_area.py ../areas/cambridge.toml --output ../build/maps/cambridge
```

Open data is downloaded to `downloads/` on first use (`fetch.py` does this on its own too): OS OpenData from the OS Downloads API, OpenStreetMap from Geofabrik, NLS tiles one by one. The area build also writes `<area>.groute` and `<area>.places.json`. Existing outputs are skipped; delete one to rebuild it.

`build_map.py` builds a single pack from any mix of sources: `--os-tiles` (zips or folders of GeoTIFFs named by grid square or quarter, eg: OpenMap Local, 1:250 000), `--world-file` (any BNG raster with a `.tfw`, eg: MiniScale), `--ozi` (OziExplorer `.map`/`.ozf2` folders), `--nls LAYER`, and `--paths GPKG`. Each source fills the level nearest its own resolution (resampled and snapped to its own colours); coarser levels are averaged down from the level below.

Six-inch NLS maps are rendered at twice the resolution and averaged down for antialiased linework, with the paper flattened to one colour and the ink reduced to an 8-step ramp. NLS maps are for personal use; ask the NLS before sharing packs made from them.

`check_gmap.py` compares a pack with OziExplorer source sheets and can render an area to PNG.

### Format

Little endian. Offsets are from the start of the file.

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

Level table, 32 bytes per level, finest first, each level half the resolution of the one before:

| Offset | Type | Field |
|---|---|---|
| 0 | u32 | millimetres per pixel |
| 4 | i32 | west edge, BNG metres |
| 8 | i32 | north edge, BNG metres |
| 12 | u32 | columns |
| 16 | u32 | rows |
| 20 | u32 | index offset |

Tiles cover `256 * metres per pixel` metres and sit on a grid anchored at BNG 0,0, so every pack with the same resolution shares tile edges. The index is `columns * rows` entries of `u32 offset, u32 length`, top row first, west to east; a length of 0 means no tile.

Tile:

| Offset | Type | Field |
|---|---|---|
| 0 | u8 | flags: bit 0, palette entry 0 is transparent |
| 1 | u8 | colour count - 1 |
| 2 | u16[count] | palette, RGB565 |
| 2 + 2 * count | | zlib stream of 256 x 256 palette indices, top row first; absent if count is 1 |

Transparent pixels are outside the source maps; draw a coarser level or another pack underneath.
