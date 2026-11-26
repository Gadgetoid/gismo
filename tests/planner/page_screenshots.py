# /// script
# requires-python = ">=3.10"
# dependencies = ["playwright"]
# ///

import functools
import http.server
import os
import sys
import threading
from pathlib import Path

from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[2]


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def main():
    output = Path(sys.argv[1])
    outing = sorted((ROOT / "build/screens/card/Outings").glob("*.zip"))
    if not (ROOT / "build/site/maps/index.json").exists() or not outing:
        sys.exit("run make site and make screens first")
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(QuietHandler, directory=str(ROOT)))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f"http://127.0.0.1:{server.server_address[1]}/build/site"
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(channel=os.environ.get("PLAYWRIGHT_CHANNEL", "chrome") or None, headless=True)
        page = browser.new_page(viewport={"width": 1100, "height": 660})
        page.goto(f"{base}/index.html")
        page.wait_for_timeout(1500)
        page.set_input_files("input[name=mission]", str(ROOT / "missions/cambridge.txt"))
        page.wait_for_timeout(500)
        page.evaluate("select(2)")
        page.locator("ol > li").nth(2).locator("input[name=route]").check()
        page.click("button[data-action=fit]")
        page.wait_for_timeout(1500)
        page.screenshot(path=str(output / "planner.png"))
        page.goto(f"{base}/viewer.html")
        page.locator("input[name=online]").uncheck()
        page.wait_for_timeout(1000)
        page.set_input_files("input[name=outing]", str(outing[-1]))
        page.wait_for_timeout(2000)
        page.screenshot(path=str(output / "viewer.png"))
        browser.close()
    server.shutdown()
    print(f"wrote {output / 'planner.png'} and {output / 'viewer.png'}")


if __name__ == "__main__":
    main()
