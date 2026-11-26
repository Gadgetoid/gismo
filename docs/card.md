# On the card

GISmo finds its folder on the storage card wherever it's installed, and keeps everything there:

```
GISmo/
    Maps/           map packs, walking routes and places (see Maps)
    Missions/       missions and their photo hints (see Mission planner)
    Filters/        camera filters
    Planner/        the mission planner and outing viewer, to open on a PC
    Sessions/       one folder per mission, "Free roam" without one
        <mission>/
            progress.txt        stops done, with times and answers
            events.csv          everything that happened, with times and positions
            tracks/*.gpx        the route, one file per outing
            photos/*.jpg        photos, tagged with where they were taken
    Outings/        a zip of each finished outing, for the outing viewer
    GISmo.exe       optional: GISmo can run from here instead of being installed
```

GPX and CSV files open in most mapping programs and spreadsheets too.

## Settings

An optional `GISmo.ini` beside `GISmo.exe` changes these:

```
track_spacing_metres = 5        log a point on the route after moving this far...
track_interval_seconds = 15     ...or after this long
warmth_interval_seconds = 8     how often "warmer" or "colder" is judged
warmth_threshold_metres = 4     how much closer or further counts
route_refresh_metres = 20       plan the walking route again after moving this far
clock_wait_seconds = 120        how long to wait for GPS time before using the Juno's clock
pan_mode_timeout_seconds = 3
metres_per_step = 0.6           for "about N steps"
max_drag_jump_pixels = 80       ignore jumps bigger than this when dragging the map
jpeg_quality = 85
log_input = no                  log buttons and drags to GISmo.log
```

## Trying it indoors

Put a recorded GPS log (NMEA) named `replay.nmea` beside `GISmo.exe`, and GISmo plays it back, over and over, instead of using the GPS. The card zip has `replay.nmea.example`, a walk in a circle around central Cambridge; rename it to try it.

## When something goes wrong

`GISmo.log`, beside `GISmo.exe`, records what GISmo did as it started and ran. It's the first thing to include in a bug report.
