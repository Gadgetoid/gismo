# /// script
# requires-python = ">=3.10"
# ///

import sys
import zipfile
from pathlib import Path


def main():
    archive_path, input_path = sys.argv[1], Path(sys.argv[2])
    with zipfile.ZipFile(archive_path) as archive:
        ok = archive.testzip() is None
        names = archive.namelist()
        ok = ok and names == ["Cambridge/summary.txt", "Cambridge/photos/caf\u00e9.bin"]
        ok = ok and archive.read(names[0]) == b"GISmo outing\n"
        ok = ok and archive.read(names[1]) == input_path.read_bytes()
        ok = ok and all(info.date_time == (2026, 11, 14, 10, 30, 0) for info in archive.infolist())
    print(f"zip: {names}, " + ("contents, CRCs and dates match" if ok else "MISMATCH"))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
