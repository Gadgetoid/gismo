# /// script
# requires-python = ">=3.10"
# ///

import sys
import zipfile
from pathlib import Path


def main():
    archives = sorted(Path(sys.argv[1]).glob("*.zip"))
    if not archives:
        print("outing: no zip saved")
        sys.exit(1)
    with zipfile.ZipFile(archives[-1]) as archive:
        names = archive.namelist()
        base = names[0].split("/")[0]
        summary = archive.read(f"{base}/summary.txt").decode()
        photos = [name for name in names if "/photos/" in name and name.endswith(".jpg")]
        tracks = [name for name in names if "/tracks/" in name and name.endswith(".gpx")]
        ok = archive.testzip() is None
        ok = ok and {f"{base}/progress.txt", f"{base}/events.csv", f"{base}/mission.txt"} <= set(names)
        ok = ok and photos and tracks
        ok = ok and "Cambridge Explorer" in summary and "Stops: 5 of 5" in summary and "Distance: 2.4 km" in summary
        ok = ok and all(archive.read(name)[:2] == b"\xff\xd8" for name in photos)
    print(f"outing: {archives[-1].name}, {len(names)} files ({len(photos)} photos, {len(tracks)} tracks), " + ("contents check out" if ok else "MISMATCH"))
    print("\n".join("    " + line for line in summary.splitlines()))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
