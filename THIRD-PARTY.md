# Third-party material

GISmo itself is MIT licensed (see LICENSE). It includes or builds on:

## In GISmo.exe and the installers

- **grimace** (https://github.com/grimace-sdk/grimace), MIT. The Windows CE toolchain, headers and runtime GISmo is built with and linked against. The self-installer (`GISmo-...-setup.exe`) is grimace's `ceinstall`, from CEM Porter.
- **LLVM compiler-rt and libc++**, Apache License 2.0 with LLVM Exceptions (https://llvm.org/LICENSE.txt), linked in through grimace's runtime.
- **Material Symbols** by Google, Apache License 2.0 (https://github.com/google/material-design-icons). The icons in `src/app/icons.h` and `assets/gismo.ico` are rendered from the Material Symbols Rounded font.

## Camera filters

- `filters/*.flt` are from **campsnap-filters** by Ryan (https://github.com/thirdr/campsnap-filters), MIT; see `filters/LICENSE-campsnap-filters`.

## Map data

- The example maps in `maps/` (and on GitHub Pages) contain **Ordnance Survey** data: OS OpenMap - Local, Contains OS data (c) Crown copyright and database right, under the Open Government Licence v3.0 (https://www.nationalarchives.gov.uk/doc/open-government-licence/version/3/).
- Footpaths, walking routes and places come from **OpenStreetMap**, (c) OpenStreetMap contributors, under the Open Database Licence (https://www.openstreetmap.org/copyright), via Geofabrik extracts.
- The outing viewer can show **OpenStreetMap** tiles from tile.openstreetmap.org, (c) OpenStreetMap contributors, under OpenStreetMap's tile usage policy (https://operations.osmfoundation.org/policies/tiles/).
- Historic maps from the **National Library of Scotland** are not distributed. `tools/build_area.py` can build them for personal use; ask the NLS before sharing packs made from them.

## Tests only

- `tests/screens/stb_truetype.h`, by Sean Barrett, public domain or MIT (https://github.com/nothings/stb).
