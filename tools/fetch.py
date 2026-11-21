# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy", "pyproj"]
# ///

import argparse
import shutil
import urllib.request
import zipfile
from pathlib import Path

from osm import geofabrik_url

OS_DOWNLOADS = "https://api.os.uk/downloads/v1/products"
USER_AGENT = "GISmo/1.0 (offline maps for a kids' GPS game)"
DEFAULT_DOWNLOADS = Path(__file__).parent.parent / "downloads"


def download(url, path):
    if path.exists():
        return path
    path.parent.mkdir(parents=True, exist_ok=True)
    partial = path.with_name(path.name + ".partial")
    print(f"fetching {url}", flush=True)
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=60) as response, open(partial, "wb") as output:
        total = int(response.headers.get("Content-Length") or 0)
        done = 0
        while chunk := response.read(1 << 20):
            output.write(chunk)
            done += len(chunk)
            if total:
                print(f"\r  {done / 1e6:.0f} of {total / 1e6:.0f} MB", end="", flush=True)
    print(flush=True)
    partial.rename(path)
    return path


def os_local(square, downloads=DEFAULT_DOWNLOADS):
    square = square.upper()
    url = f"{OS_DOWNLOADS}/OpenMapLocal/downloads?area={square}&format=GeoTIFF&subformat=Full%20Colour&redirect"
    return download(url, downloads / "os" / f"openmap-local-{square}.zip")


def os_250k(downloads=DEFAULT_DOWNLOADS):
    return download(f"{OS_DOWNLOADS}/250kScaleColourRaster/downloads?area=GB&format=TIFF-LZW&redirect", downloads / "os" / "250k.zip")


def os_miniscale(downloads=DEFAULT_DOWNLOADS):
    folder = downloads / "os" / "miniscale"
    found = sorted(folder.rglob("MiniScale_standard_*.tif")) if folder.exists() else []
    if not found:
        archive = download(f"{OS_DOWNLOADS}/MiniScale/downloads?area=GB&format=Zip%20file%20%28containing%20EPS%2C%20Illustrator%20and%20TIFF-LZW%29&redirect", downloads / "os" / "miniscale.zip")
        with zipfile.ZipFile(archive) as opened:
            members = [member for member in opened.namelist() if "RGB_TIF" in member and member.lower().endswith((".tif", ".tfw"))]
            opened.extractall(folder, members)
        found = sorted(folder.rglob("MiniScale_standard_*.tif"))
    return found[-1]


def osm(region, downloads=DEFAULT_DOWNLOADS):
    name = region.strip("/").split("/")[-1]
    gpkg = downloads / "osm" / f"{name}.gpkg"
    if gpkg.exists():
        return gpkg
    archive = download(geofabrik_url(region), downloads / "osm" / f"{name}-latest-free.gpkg.zip")
    with zipfile.ZipFile(archive) as opened:
        member = next(member for member in opened.namelist() if member.endswith(".gpkg"))
        with opened.open(member) as source, open(gpkg, "wb") as target:
            shutil.copyfileobj(source, target)
    return gpkg


def main():
    parser = argparse.ArgumentParser(description="Download open map data for GISmo map packs")
    parser.add_argument("--downloads", type=Path, default=DEFAULT_DOWNLOADS)
    commands = parser.add_subparsers(dest="command", required=True)
    local = commands.add_parser("os-local", help="OS OpenMap - Local raster, by 100 km square")
    local.add_argument("squares", nargs="+", help="eg: TL")
    commands.add_parser("os-250k", help="OS 1:250 000 Scale Colour Raster, all of Great Britain")
    commands.add_parser("os-miniscale", help="OS MiniScale, all of Great Britain")
    geofabrik = commands.add_parser("osm", help="Geofabrik OpenStreetMap extract")
    geofabrik.add_argument("region", help="Geofabrik path, eg: europe/united-kingdom/england/cambridgeshire")
    args = parser.parse_args()

    if args.command == "os-local":
        for square in args.squares:
            print(os_local(square, args.downloads))
    elif args.command == "os-250k":
        print(os_250k(args.downloads))
    elif args.command == "os-miniscale":
        print(os_miniscale(args.downloads))
    else:
        print(osm(args.region, args.downloads))


if __name__ == "__main__":
    main()
