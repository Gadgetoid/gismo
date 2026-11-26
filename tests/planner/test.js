"use strict";

const assert = require("node:assert");
const childProcess = require("node:child_process");
const crypto = require("node:crypto");
const os = require("node:os");
const fs = require("node:fs");
const path = require("node:path");
const gismo = require("../../planner/gismo.js");

const root = path.resolve(__dirname, "../..");
const expectations = JSON.parse(process.argv[2]);
let failures = 0;

function check(name, run) {
    try {
        run();
        console.log(`ok   ${name}`);
    } catch (error) {
        failures++;
        console.log(`FAIL ${name}: ${error.message}`);
    }
}

async function checkAsync(name, run) {
    try {
        await run();
        console.log(`ok   ${name}`);
    } catch (error) {
        failures++;
        console.log(`FAIL ${name}: ${error.message}`);
    }
}

async function main() {
    const vectors = fs.readFileSync(path.join(root, "tests/host/grid_vectors.csv"), "utf8").trim().split("\n").map(line => line.split(",").map(Number));

    check("WGS84 to grid within 1 cm of pyproj", () => {
        let worst = 0;
        for (const [latitude, longitude, east, north] of vectors) {
            const grid = gismo.wgs84ToGrid(latitude, longitude);
            worst = Math.max(worst, Math.hypot(grid.east - east, grid.north - north));
        }
        assert.ok(worst < 0.01, `worst ${worst.toFixed(4)} m`);
        console.log(`     worst ${(worst * 1000).toFixed(1)} mm over ${vectors.length} points`);
    });

    check("grid to WGS84 round trip within 5 cm", () => {
        let worst = 0;
        for (const [latitude, longitude, east, north] of vectors) {
            const back = gismo.gridToWgs84(east, north);
            const metres = Math.hypot((back.latitude - latitude) * 111320, (back.longitude - longitude) * 111320 * Math.cos(latitude * Math.PI / 180));
            worst = Math.max(worst, metres);
        }
        assert.ok(worst < 0.05, `worst ${worst.toFixed(4)} m`);
        console.log(`     worst ${(worst * 1000).toFixed(1)} mm`);
    });

    check("grid references", () => {
        assert.strictEqual(gismo.gridReference(545088.4, 258462.9, 6), "TL 450 584");
        assert.strictEqual(gismo.gridReference(338044.0, 972651.0, 6), "ND 380 726");
        assert.strictEqual(gismo.gridReference(-5, 100, 6), "");
    });

    const cambridge = fs.readFileSync(path.join(root, "missions/cambridge.txt"), "utf8");
    check("Cambridge mission parses like the device", () => {
        const mission = gismo.parseMission(cambridge);
        assert.strictEqual(mission.title, "Cambridge Explorer");
        assert.strictEqual(mission.waypoints.length, 5);
        assert.deepStrictEqual(mission.waypoints[2].choices, ["Grasshopper", "Dragon", "Owl", "Cat"]);
        assert.strictEqual(mission.waypoints[2].answer, "Grasshopper");
        assert.strictEqual(mission.waypoints[1].radius, 40);
    });

    check("write then parse round trips", () => {
        const mission = gismo.parseMission(cambridge);
        mission.waypoints[0].clue = "Line one\nline two";
        mission.waypoints[0].photo = "church.bmp";
        const written = gismo.writeMission(mission);
        const reparsed = gismo.parseMission(written);
        assert.deepStrictEqual(reparsed, mission);
        assert.strictEqual(gismo.writeMission(reparsed), written);
        assert.ok(written.includes("clue = Line one\\nline two"));
    });

    check("errors match the device parser", () => {
        const errors = [
            ["title = T\n[A]\ntask = visit\n", /no location/],
            ["[A]\nlocation = 52.1\n", /line 2/],
            ["[A]\nlocation = 52.1, 0.1\ntask = dance\n", /line 3/],
            ["[A]\nlocation = 52.1, 0.1\ntask = choice\nchoices = a | b\nanswer = c\n", /isn't one of/],
            ["colour = blue\n", /unknown mission key/],
            ["# nothing\n", /no waypoints/],
        ];
        for (const [text, pattern] of errors) {
            assert.throws(() => gismo.parseMission(text), pattern);
        }
    });

    check("problems flag text the Juno would cut short", () => {
        const stop = name => ({name, latitude: 52, longitude: 0, radius: 25, task: "visit", route: false, clue: "", prompt: "", choices: [], answer: "", photo: ""});
        const mission = {title: "T", intro: "", waypoints: [stop("Gate")]};
        assert.deepStrictEqual(gismo.missionProblems(mission), []);
        mission.waypoints[0].prompt = "x".repeat(159);
        assert.deepStrictEqual(gismo.missionProblems(mission), []);
        mission.waypoints[0].prompt = "x".repeat(158) + "\u00e9";
        assert.ok(gismo.missionProblems(mission).some(problem => /prompt is too long.*Cut about 1 letters/.test(problem)));
        mission.waypoints[0].prompt = "";
        Object.assign(mission.waypoints[0], {task: "choice", choices: ["a", "b", "c", "d", "e", "f", "g"], answer: "a"});
        assert.ok(gismo.missionProblems(mission).some(problem => problem.includes("at most 6 choices")));
        mission.waypoints[0].choices = ["a", "y".repeat(40)];
        assert.ok(gismo.missionProblems(mission).some(problem => problem.includes("choice") && problem.includes("too long")));
        mission.waypoints = Array.from({length: 33}, (_, index) => stop(`Stop ${index + 1}`));
        assert.ok(gismo.missionProblems(mission).some(problem => problem.includes("at most 32 waypoints")));
    });

    check("problems flag unsaveable missions", () => {
        const mission = {title: "T", intro: "", waypoints: [{name: "", latitude: 52, longitude: 0, radius: 25, task: "choice", clue: "", prompt: "", choices: ["a"], answer: "", photo: ""}]};
        const problems = gismo.missionProblems(mission);
        assert.ok(problems.some(problem => problem.includes("needs a name")));
        assert.ok(problems.some(problem => problem.includes("two choices")));
    });

    await checkAsync("map pack tiles decode like the Python reader", async () => {
        const buffer = fs.readFileSync(process.argv[3]);
        const pack = new gismo.MapPack(buffer.buffer.slice(buffer.byteOffset, buffer.byteOffset + buffer.byteLength), path.basename(process.argv[3]));
        let compared = 0;
        for (const [key, expected] of Object.entries(expectations.tiles)) {
            const [level, column, row] = key.split("/").map(Number);
            const tile = await pack.decode(level, column, row);
            const rgba = Uint8Array.from(tile.rgba);
            for (let pixel = 0; pixel < rgba.length; pixel += 4) {
                if (rgba[pixel + 3] === 0) {
                    rgba[pixel] = rgba[pixel + 1] = rgba[pixel + 2] = 0;
                }
            }
            assert.strictEqual(crypto.createHash("sha1").update(rgba).digest("hex"), expected, `tile ${key}`);
            compared++;
        }
        assert.ok(compared > 50);
        assert.strictEqual(pack.bestLevel(3), 1);
        assert.strictEqual(pack.bestLevel(9), 2);
        assert.strictEqual(pack.bestLevel(10000), pack.levels.length - 1);
        console.log(`     ${compared} tiles identical`);
    });

    check("walking routes cost the same as a Python Dijkstra", () => {
        const buffer = fs.readFileSync(process.argv[4]);
        const graph = new gismo.RouteGraph(buffer.buffer.slice(buffer.byteOffset, buffer.byteOffset + buffer.byteLength), path.basename(process.argv[4]));
        let found = 0;
        for (const [start, goal, expected] of expectations.routes) {
            const route = graph.find(start, goal);
            assert.strictEqual(route ? route.cost : null, expected, `${start} -> ${goal}`);
            found += route ? 1 : 0;
        }
        assert.ok(found > 6);
        const middle = {east: (graph.bounds.west + graph.bounds.east) / 2, north: (graph.bounds.south + graph.bounds.north) / 2};
        assert.ok(graph.covers(middle.east, middle.north) && !graph.covers(0, 0));
        console.log(`     ${found} of ${expectations.routes.length} routes found`);
    });

    check("via points and routes round trip", () => {
        const text = "title = V\n[Start]\nlocation = 52.1, 0.1\n[]\nlocation = 52.11, 0.1\ntask = via\nroute = yes\n";
        const mission = gismo.parseMission(text);
        assert.strictEqual(mission.waypoints[1].name, "");
        assert.strictEqual(mission.waypoints[1].route, true);
        assert.deepStrictEqual(gismo.parseMission(gismo.writeMission(mission)), mission);
        assert.ok(gismo.writeMission(mission).includes("[]\nlocation = 52.110000, 0.100000\ntask = via\nroute = yes"));
        assert.deepStrictEqual(gismo.missionProblems(mission), []);
        assert.throws(() => gismo.parseMission("[]\nlocation = 52.1, 0.1\n"), /needs a name/);
        assert.throws(() => gismo.parseMission("[A]\nlocation = 52.1, 0.1\nroute = maybe\n"), /line 3/);
    });

    await checkAsync("unzip reads stored and deflated zips", async () => {
        const stored = gismo.zip([{name: "outing/summary.txt", bytes: "hello"}, {name: "outing/photos/a.jpg", bytes: new Uint8Array([1, 2, 3])}]);
        const files = await gismo.unzip(stored.buffer);
        assert.deepStrictEqual([...files.keys()], ["outing/summary.txt", "outing/photos/a.jpg"]);
        assert.strictEqual(new TextDecoder().decode(files.get("outing/summary.txt")), "hello");
        const script = "import io, sys, zipfile\nbuffer = io.BytesIO()\nwith zipfile.ZipFile(buffer, 'w', zipfile.ZIP_DEFLATED) as archive:\n    archive.writestr('trip/events.csv', 'time,event\\n' * 200)\nsys.stdout.buffer.write(buffer.getvalue())";
        const deflated = childProcess.execFileSync("python3", ["-I", "-c", script]);
        const inflated = await gismo.unzip(deflated.buffer.slice(deflated.byteOffset, deflated.byteOffset + deflated.byteLength));
        assert.strictEqual(new TextDecoder().decode(inflated.get("trip/events.csv")), "time,event\n".repeat(200));
    });

    check("photo positions come from the device encoder's EXIF", () => {
        const folder = fs.mkdtempSync(path.join(os.tmpdir(), "gismo-exif-"));
        try {
            fs.writeFileSync(path.join(folder, "input.raw"), Buffer.alloc(320 * 240 * 2, 0x55));
            childProcess.execFileSync(path.join(root, "tests/host/jpeg_tool"), [path.join(folder, "input.raw"), "320", "240", "85", path.join(folder, "photo.jpg")]);
            const position = gismo.exifPosition(new Uint8Array(fs.readFileSync(path.join(folder, "photo.jpg"))));
            assert.ok(Math.abs(position.latitude - 52.2525051) < 1e-6 && Math.abs(position.longitude - 0.0184061) < 1e-6);
            assert.ok(Math.abs(position.altitude - 21.4) < 0.05);
            assert.strictEqual(new Date(position.time).toISOString(), "2026-11-14T12:42:15.000Z");
            assert.strictEqual(gismo.exifPosition(new Uint8Array([0xFF, 0xD8, 0xFF, 0xD9])), null);
        } finally {
            fs.rmSync(folder, {recursive: true});
        }
    });

    check("GPX tracks, events CSV and track stats", () => {
        const gpx = '<gpx><trk><trkseg>\n<trkpt lat="52.2000000" lon="0.1200000"><ele>10.0</ele><time>2026-11-14T10:00:00.000Z</time></trkpt>\n<trkpt lon="0.1200000" lat="52.2010000"><time>2026-11-14T10:01:30.000Z</time></trkpt>\n</trkseg></trk></gpx>';
        const points = gismo.parseGpx(gpx);
        assert.strictEqual(points.length, 2);
        assert.strictEqual(points[1].latitude, 52.201);
        assert.strictEqual(points[0].elevation, 10);
        const stats = gismo.trackStats([points]);
        assert.ok(Math.abs(stats.metres - 111.3) < 1, `${stats.metres}`);
        assert.strictEqual(stats.seconds, 90);
        const rows = gismo.parseCsv('time,latitude,longitude,event,waypoint,detail\r\n2026-11-14T10:00:00.000Z,52.2,0.12,answer,"Post ""Box""","line one\nline two"\n');
        assert.deepStrictEqual(rows[1], ["2026-11-14T10:00:00.000Z", "52.2", "0.12", "answer", 'Post "Box"', "line one\nline two"]);
    });

    check("zip opens in Python with the right contents", () => {
        const bytes = crypto.randomBytes(5000);
        const archive = gismo.zip([{name: "Missions/cambridge.txt", bytes: cambridge}, {name: "Missions/café.bmp", bytes}], new Date(2026, 10, 14, 12, 34, 56));
        const zipPath = path.join(os.tmpdir(), `gismo-test-${process.pid}.zip`);
        fs.writeFileSync(zipPath, archive);
        try {
            const script = "import hashlib, sys, zipfile\nwith zipfile.ZipFile(sys.argv[1]) as archive:\n    assert archive.testzip() is None\n    for info in archive.infolist():\n        print(info.filename, info.date_time, hashlib.sha1(archive.read(info)).hexdigest())";
            const listing = childProcess.execFileSync("python3", ["-I", "-c", script, zipPath], {encoding: "utf8"}).trim().split("\n");
            const sha1 = data => crypto.createHash("sha1").update(data).digest("hex");
            assert.deepStrictEqual(listing, [
                `Missions/cambridge.txt (2026, 11, 14, 12, 34, 56) ${sha1(cambridge)}`,
                `Missions/café.bmp (2026, 11, 14, 12, 34, 56) ${sha1(bytes)}`,
            ]);
        } finally {
            fs.unlinkSync(zipPath);
        }
    });

    check("photo hint BMP matches make_hint.py", () => {
        const rgba = new Uint8ClampedArray(320 * 240 * 4);
        for (let y = 0; y < 240; y++) {
            for (let x = 0; x < 320; x++) {
                const pixel = (y * 320 + x) * 4;
                rgba[pixel] = Math.floor(x * 255 / 319);
                rgba[pixel + 1] = Math.floor(y * 255 / 239);
                rgba[pixel + 2] = 128;
                rgba[pixel + 3] = 255;
            }
        }
        assert.strictEqual(crypto.createHash("sha1").update(gismo.bmp565(rgba, 320, 240)).digest("hex"), expectations.bmp);
    });

    console.log(failures ? `${failures} failures` : "all passed");
    process.exit(failures ? 1 : 0);
}

main();
