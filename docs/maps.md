# Maps

GISmo's maps are map packs (`.gmap`) in the card's `Maps` folder, one per layer: a detailed map, an optional old map, and an optional overview for when you zoom out. Beside them go the area's walking routes (`.groute`), the places worth visiting for exploring (`.gplaces`), and the full list of places for the planner (`.places.json`).

Releases include central Cambridge. The tools in `tools/` make the same for anywhere in Great Britain, from open data they download themselves. They need Python and [uv](https://docs.astral.sh/uv/).

## Making an area

An area file describes one place:

```toml
name = "Cambridge"
bbox = [543000, 256000, 547000, 260000]
osm = "europe/united-kingdom/england/cambridgeshire"

[[maps]]
source = "os-local"
paths = true

[[maps]]
suffix = "1900"
source = "nls"
layer = "6inch"

[[maps]]
suffix = "overview"
source = "os-overview"
margin = 40000
```

| Key | |
|---|---|
| `bbox` | British National Grid metres: west, south, east, north |
| `osm` | the [Geofabrik](https://download.geofabrik.de/europe/united-kingdom.html) extract for footpaths, walking routes and places, or a list of them for an area that crosses county lines |
| `route_tile` | optional: split walking routes into tiles of this many metres, with a quarter of a tile overlapping, to keep each small enough for the Juno. Worth it for areas over about 20 km across |
| `source` | `os-local` (OS OpenMap Local, 2 m per pixel), `os-overview` (OS 1:250 000 and MiniScale) or `nls` (historic OS maps from the National Library of Scotland) |
| `paths` | draw OpenStreetMap footpaths (red-orange dashes) and tracks (brown dashes) on the map |
| `layer` | for `nls`: `6inch`, `6inch1880`, `1inch1890`, `1inch1900` or `1inch1940` |
| `suffix`, `name`, `margin` | the pack's file suffix and name, and how many metres beyond `bbox` it covers |

Then:

```
cd tools
uv run build_area.py ../areas/cambridge.toml --output ../build/maps/cambridge
```

Copy what's in the output folder to the card's `Maps` folder. Map data is downloaded to `downloads/` the first time and kept. Outputs that already exist are skipped, so delete one to rebuild it.

Old maps from the National Library of Scotland are for personal use. Ask the NLS before sharing packs made from them.

## Walking routes

Routes follow paths, footways, cycleways, bridleways, steps and tracks where they can. Quiet streets cost a little more, service roads and main roads more, roads at 40 mph or more more again, and motorways and fast trunk roads are left out. GISmo won't draw a route when either end is more than 150 m from a path or road.

## Places

Exploring lists playgrounds and parks; monuments, memorials, ruins, mills and churches; viewpoints, hill tops, springs and ponds; and museums, artwork, fountains and attractions, from OpenStreetMap. Shops, food and services are left out. The planner's Places list has everything with a name.

## Other tools

- `fetch.py` downloads OS OpenData and Geofabrik extracts on its own.
- `build_map.py` builds a single pack from any mix of sources, including OziExplorer maps.
- `check_gmap.py` renders part of a pack to PNG.

Each has `--help`.
