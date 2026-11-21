# /// script
# requires-python = ">=3.10"
# dependencies = ["playwright"]
# ///

import functools
import http.server
import io
import math
import os
import re
import struct
import subprocess
import tempfile
import sys
import threading
import zipfile
import zlib
from pathlib import Path

from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[2]
failures = 0
online_requests = []


def png_tile():
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    rows = b"".join(b"\x00" + bytes([200, 220, 240]) * 256 for _ in range(256))
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 256, 256, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")


TILE = png_tile()


def route_online(page):
    def answer(route):
        online_requests.append(route.request.url)
        route.fulfill(status=200, content_type="image/png", body=TILE)
    page.route("https://tile.openstreetmap.org/**", answer)


def check(name, ok, detail=""):
    global failures
    failures += 0 if ok else 1
    print(f"{'ok  ' if ok else 'FAIL'} {name}{f': {detail}' if detail else ''}")


def serve():
    class QuietHandler(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *args):
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(QuietHandler, directory=str(ROOT)))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


def open_planner(browser, base, errors):
    page = browser.new_page(viewport={"width": 1280, "height": 800}, accept_downloads=True)
    page.on("pageerror", lambda error: errors.append(str(error)))
    route_online(page)
    page.goto(f"{base}/planner/index.html")
    page.evaluate("delete window.showSaveFilePicker")
    page.set_input_files("input[name=maps]", [str(ROOT / "maps/cambridge.gmap"), str(ROOT / "maps/cambridge.groute"), str(ROOT / "maps/cambridge.places.json")])
    page.set_input_files("input[name=mission]", str(ROOT / "missions/cambridge.txt"))
    page.wait_for_timeout(1000)
    page.evaluate("[...document.querySelectorAll('ol details')].forEach(details => details.open = false)")
    return page


def names(page):
    return page.evaluate("state.mission.waypoints.map(waypoint => waypoint.name)")


def screen_point(page, index):
    box = page.locator("canvas").bounding_box()
    point = page.evaluate(f"(() => {{ const grid = waypointGrid(state.mission.waypoints[{index}]); return toScreen(grid.east, grid.north); }})()")
    return box["x"] + point["x"], box["y"] + point["y"]


def test_zoom(page):
    box = page.locator("canvas").bounding_box()
    before = page.evaluate("mapView.view.metresPerPixel")
    page.mouse.move(box["x"] + 400, box["y"] + 300)
    for _ in range(5):
        page.mouse.wheel(0, 20)
    page.wait_for_timeout(200)
    after_wheel = page.evaluate("mapView.view.metresPerPixel")
    page.dispatch_event("canvas", "wheel", {"deltaY": 100, "ctrlKey": True, "clientX": box["x"] + 400, "clientY": box["y"] + 300})
    after_pinch = page.evaluate("mapView.view.metresPerPixel")
    check("100 px of wheel zooms smoothly by about 1.26x", abs(after_wheel / before - 2 ** (1 / 3)) < 0.01, f"x{after_wheel / before:.3f}")
    check("a 100 px pinch doubles the scale", abs(after_pinch / after_wheel - 2) < 0.01, f"x{after_pinch / after_wheel:.3f}")
    page.click("button[data-action=fit]")
    page.wait_for_timeout(300)


def test_insert_on_line(page):
    first = screen_point(page, 2)
    second = screen_point(page, 3)
    middle = ((first[0] + second[0]) / 2, (first[1] + second[1]) / 2 + 2)
    page.mouse.move(*middle)
    check("the line between waypoints shows an insert cursor", page.evaluate("canvas.hasAttribute('data-insert')"))
    page.mouse.click(*middle)
    page.wait_for_timeout(200)
    check("clicking the line inserts a waypoint there", names(page)[3].startswith("Waypoint") and len(names(page)) == 6, str(names(page)))
    page.keyboard.press("Control+z")


def test_drag_reorder(page):
    summaries = page.locator("ol > li summary")
    start = summaries.nth(4).bounding_box()
    target = summaries.nth(0).bounding_box()
    page.mouse.move(start["x"] + 30, start["y"] + 10)
    page.mouse.down()
    for step in range(1, 11):
        page.mouse.move(start["x"] + 30, start["y"] + 10 + (target["y"] + 3 - start["y"] - 10) * step / 10)
    page.mouse.up()
    page.wait_for_timeout(200)
    check("dragging the last waypoint above the first reorders them", names(page)[0] == "Reality Checkpoint", str(names(page)))
    page.keyboard.press("Control+z")
    check("undo puts the order back", names(page)[0] == "Round Church")


def test_shortcuts(page):
    page.evaluate("select(1)")
    page.locator("h2#waypoints-heading").click()
    page.keyboard.press("Delete")
    check("Delete removes the selected waypoint", "King's College Chapel" not in names(page))
    page.keyboard.press("Control+z")
    check("undo restores it", names(page)[1] == "King's College Chapel")
    page.keyboard.press("Control+Shift+z")
    check("redo removes it again", len(names(page)) == 4)
    page.keyboard.press("Control+z")
    page.evaluate("select(0)")
    clue = page.locator("ol > li").nth(0).locator("textarea[name=clue]")
    original = clue.input_value()
    clue.click()
    page.keyboard.press("End")
    page.keyboard.type(" More words.", delay=20)
    page.keyboard.press("Delete")
    check("Delete while typing leaves the waypoints alone", len(names(page)) == 5)
    page.locator("h2#waypoints-heading").click()
    page.keyboard.press("Control+z")
    check("one undo removes a burst of typing", clue.input_value() == original)
    before = page.evaluate("[state.mission.waypoints[4].latitude, state.mission.waypoints[4].longitude]")
    x, y = screen_point(page, 4)
    page.mouse.move(x, y)
    page.mouse.down()
    page.mouse.move(x + 50, y + 30, steps=8)
    page.mouse.up()
    page.keyboard.press("Control+z")
    check("undo moves a dragged marker back", page.evaluate("[state.mission.waypoints[4].latitude, state.mission.waypoints[4].longitude]") == before)


def test_route_preview(page):
    page.evaluate("select(1)")
    item = page.locator("ol > li").nth(1)
    item.locator("input[name=route]").check()
    page.wait_for_timeout(300)
    where = item.locator("output[name=where]").text_content()
    check("a route leg shows its walking distance", "walking route" in where, where)
    third = page.locator("ol > li").nth(2)
    page.evaluate("select(2)")
    third.locator("select[name=task]").select_option("via")
    third.locator("input[name=name]").fill("")
    page.wait_for_timeout(200)
    check("route points hide the clue", not third.locator("textarea[name=clue]").is_visible())
    check("route points are left out of the numbering", page.locator("ol > li summary").nth(3).inner_text().startswith("3."))
    check("a nameless route point is a valid mission", page.locator("[role=status]").inner_text() == "")
    third.locator("select[name=task]").select_option("choice")
    page.wait_for_timeout(100)
    check("choice tasks show their choices", third.locator("textarea[name=choices]").is_visible())


def test_export_and_backup(page):
    with page.expect_download() as info:
        page.click("button[data-action=export-mission]")
    with zipfile.ZipFile(io.BytesIO(Path(info.value.path()).read_bytes())) as archive:
        check("Export zip holds the mission", archive.testzip() is None and archive.namelist()[0].startswith("Missions/"), str(archive.namelist()))
    page.keyboard.press("Escape")
    page.evaluate("""(async () => {
        const card = await navigator.storage.getDirectory();
        const write = async (folder, name, text) => { const writer = await (await folder.getFileHandle(name, {create: true})).createWritable(); await writer.write(text); await writer.close(); };
        await write(await card.getDirectoryHandle("Missions", {create: true}), "cambridge.txt", "title = T\\n");
        const session = await (await card.getDirectoryHandle("Sessions", {create: true})).getDirectoryHandle("Free roam", {create: true});
        await write(session, "events.csv", "time\\n");
        state.card = card;
    })()""")
    with page.expect_download() as info:
        page.evaluate("backUpCard()")
    with zipfile.ZipFile(io.BytesIO(Path(info.value.path()).read_bytes())) as archive:
        check("Back up card holds Missions and Sessions", sorted(archive.namelist()) == ["Missions/cambridge.txt", "Sessions/Free roam/events.csv"], str(archive.namelist()))


def outing_files(folder):
    raw = folder / "input.raw"
    raw.write_bytes(bytes([0x55, 0x55]) * 320 * 240)
    subprocess.run([str(ROOT / "tests/host/jpeg_tool"), str(raw), "320", "240", "85", str(folder / "photo.jpg")], check=True, capture_output=True)
    gpx = "".join(f'<trkpt lat="{52.2525 + step * 0.0002:.7f}" lon="0.0184061"><time>2026-11-14T12:{30 + step:02d}:00.000Z</time></trkpt>\n' for step in range(13))
    return {
        "trip/summary.txt": "GISmo outing: Bar Hill walk\n\nDistance: 0.3 km\n",
        "trip/mission.txt": "title = Bar Hill walk\n[Church]\nlocation = 52.2525051, 0.0184061\ntask = photo\n[]\nlocation = 52.2540, 0.0184\ntask = via\n[Shop]\nlocation = 52.2550, 0.0184\n",
        "trip/progress.txt": "[Church]\ndone = 2026-11-14T12:42:15.000Z\n",
        "trip/events.csv": "time,latitude,longitude,event,waypoint,detail\n2026-11-14T12:30:00.000Z,52.2525,0.0184,start,,Bar Hill walk\n2026-11-14T12:41:00.000Z,52.2525,0.0184,arrived,Church,photo\n2026-11-14T12:42:15.000Z,52.2525,0.0184,photo,Church,\"photo.jpg, Sepia\"\n2026-11-14T12:42:20.000Z,52.2525,0.0184,completed,Church,photo.jpg\n",
        "trip/tracks/20261114-123000Z.gpx": f'<?xml version="1.0"?>\n<gpx><trk><trkseg>\n{gpx}</trkseg></trk></gpx>\n',
        "trip/photos/photo.jpg": (folder / "photo.jpg").read_bytes(),
    }


def test_viewer(browser, base, errors):
    with tempfile.TemporaryDirectory() as temporary:
        folder = Path(temporary)
        files = outing_files(folder)
        for method, label in ((zipfile.ZIP_STORED, "a stored zip"), (zipfile.ZIP_DEFLATED, "a deflated zip"), (None, "loose files")):
            page = browser.new_page(viewport={"width": 1280, "height": 800})
            page.on("pageerror", lambda error: errors.append(str(error)))
            route_online(page)
            online_requests.clear()
            page.goto(f"{base}/planner/viewer.html")
            page.wait_for_timeout(300)
            if method == zipfile.ZIP_STORED:
                check("the viewer fetches nothing until an outing is open", not online_requests, f"{len(online_requests)} requests")
            if method is None:
                paths = []
                for name, data in files.items():
                    path = folder / "loose" / name.split("/")[-1]
                    path.parent.mkdir(exist_ok=True)
                    path.write_bytes(data.encode() if isinstance(data, str) else data)
                    paths.append(str(path))
                page.set_input_files("input[name=outing]", paths)
            else:
                archive_path = folder / f"outing-{label.split()[1]}.zip"
                with zipfile.ZipFile(archive_path, "w", method) as archive:
                    for name, data in files.items():
                        archive.writestr(name, data)
                page.set_input_files("input[name=outing]", str(archive_path))
            page.wait_for_timeout(800)
            stats = dict(zip(page.locator("aside dt").all_inner_texts(), page.locator("aside dd").all_inner_texts()))
            events = page.locator("aside ol li").all_inner_texts()
            check(f"viewer opens a {label}", page.locator("aside h2").first.inner_text() == "Bar Hill walk" and stats.get("Time") == "12 min" and stats.get("Stops found") == "1 of 2", str(stats))
            check(f"viewer places the photo from its EXIF ({label})", page.evaluate("state.photos.length === 1 && Math.abs(state.photos[0].latitude - 52.2525051) < 1e-6"))
            check(f"viewer lists what happened ({label})", len(events) == 4 and "Took a photo at Church" in events[2], str(events))
            if method == zipfile.ZIP_STORED:
                page.wait_for_timeout(300)
                tiles = [tuple(map(int, re.search(r"/(\d+)/(\d+)/(\d+)\.png", url).groups())) for url in online_requests]
                expected_x = lambda zoom: int((0.0184061 + 180) / 360 * 2 ** zoom)
                covering = tiles and all(14 <= zoom <= 19 for zoom, _, _ in tiles) and any(abs(x - expected_x(zoom)) <= 1 for zoom, x, _ in tiles)
                check("without GISmo maps the viewer shows OpenStreetMap at street zoom", bool(covering), f"{len(tiles)} tiles, e.g. {tiles[:2]}")
                check("the viewer credits OpenStreetMap", "OpenStreetMap" in page.locator("figcaption small").inner_text())
                page.locator("input[name=online]").uncheck()
                online_requests.clear()
                page.evaluate("mapView.zoomCentre(0.5)")
                page.wait_for_timeout(300)
                check("unticking OpenStreetMap stops tile requests", not online_requests and "OpenStreetMap" not in page.locator("figcaption small").inner_text())
                page.locator("aside ol li button").nth(2).click()
                page.wait_for_timeout(200)
                caption = page.locator("dialog figcaption").inner_text()
                check("clicking a photo event shows the photo", page.locator("dialog").is_visible() and "12:42" in caption, caption)
            page.close()


def test_online_zoom(browser, base, errors):
    with tempfile.TemporaryDirectory() as temporary:
        folder = Path(temporary)
        archive_path = folder / "outing.zip"
        with zipfile.ZipFile(archive_path, "w") as archive:
            for name, data in outing_files(folder).items():
                archive.writestr(name, data)
        page = browser.new_page(viewport={"width": 1920, "height": 1200}, device_scale_factor=2)
        page.on("pageerror", lambda error: errors.append(str(error)))
        route_online(page)
        page.goto(f"{base}/planner/viewer.html")
        page.set_input_files("input[name=outing]", str(archive_path))
        page.wait_for_timeout(800)
        page.evaluate("""(() => {
            window.onlineDraws = 0;
            const draw = mapView.drawOnlineImage.bind(mapView);
            mapView.drawOnlineImage = (...parameters) => { window.onlineDraws++; draw(...parameters); };
        })()""")
        blank = []
        for step, factor in enumerate([2] * 9 + [0.5] * 9):
            frame = page.evaluate(f"(() => {{ mapView.zoomCentre({factor}); window.onlineDraws = 0; mapView.render(); return [window.onlineDraws, mapView.metresPerPixel()]; }})()")
            if frame[0] == 0:
                blank.append(round(frame[1], 1))
            page.wait_for_timeout(150)
        check("OpenStreetMap stays drawn while zooming in and out on a large screen", not blank, f"blank at {blank} m/px")
        page.close()


def main():
    server = serve()
    base = f"http://127.0.0.1:{server.server_address[1]}"
    channel = os.environ.get("PLAYWRIGHT_CHANNEL", "chrome")
    errors = []
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(channel=channel or None, headless=True)
        for test in (test_zoom, test_insert_on_line, test_drag_reorder, test_shortcuts, test_route_preview, test_export_and_backup):
            page = open_planner(browser, base, errors)
            test(page)
            page.close()
        check("the planner doesn't fetch OpenStreetMap tiles", not online_requests, f"{len(online_requests)} requests")
        test_viewer(browser, base, errors)
        test_online_zoom(browser, base, errors)
        browser.close()
    server.shutdown()
    check("no page errors", not errors, "; ".join(errors))
    print("all passed" if not failures else f"{failures} failures")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
