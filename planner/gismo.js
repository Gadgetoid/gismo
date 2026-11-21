"use strict";

const gismo = (() => {
    const degrees = Math.PI / 180;
    const arcSeconds = degrees / 3600;
    const grs80 = {a: 6378137.0, b: 6356752.314140};
    const airy1830 = {a: 6377563.396, b: 6356256.909};
    const toOsgb36 = {tx: -446.448, ty: 125.157, tz: -542.060, s: 20.4894e-6, rx: -0.1502 * arcSeconds, ry: -0.2470 * arcSeconds, rz: -0.8421 * arcSeconds};
    const toWgs84 = {tx: 446.448, ty: -125.157, tz: 542.060, s: -20.4894e-6, rx: 0.1502 * arcSeconds, ry: 0.2470 * arcSeconds, rz: 0.8421 * arcSeconds};
    const scaleFactor = 0.9996012717;
    const originLatitude = 49 * degrees;
    const originLongitude = -2 * degrees;
    const falseEasting = 400000;
    const falseNorthing = -100000;
    const gridLetters = "ABCDEFGHJKLMNOPQRSTUVWXYZ";

    function eccentricitySquared(shape) {
        return (shape.a * shape.a - shape.b * shape.b) / (shape.a * shape.a);
    }

    function toCartesian(latitude, longitude, height, shape) {
        const e2 = eccentricitySquared(shape);
        const radius = shape.a / Math.sqrt(1 - e2 * Math.sin(latitude) ** 2);
        return {
            x: (radius + height) * Math.cos(latitude) * Math.cos(longitude),
            y: (radius + height) * Math.cos(latitude) * Math.sin(longitude),
            z: ((1 - e2) * radius + height) * Math.sin(latitude),
        };
    }

    function toGeodetic(point, shape) {
        const e2 = eccentricitySquared(shape);
        const p = Math.hypot(point.x, point.y);
        const longitude = Math.atan2(point.y, point.x);
        let latitude = Math.atan2(point.z, p * (1 - e2));
        for (let iteration = 0; iteration < 10; iteration++) {
            const radius = shape.a / Math.sqrt(1 - e2 * Math.sin(latitude) ** 2);
            const next = Math.atan2(point.z + e2 * radius * Math.sin(latitude), p);
            const change = Math.abs(next - latitude);
            latitude = next;
            if (change < 1e-12) {
                break;
            }
        }
        return {latitude, longitude};
    }

    function helmert(point, parameters) {
        const scale = 1 + parameters.s;
        return {
            x: parameters.tx + scale * point.x - parameters.rz * point.y + parameters.ry * point.z,
            y: parameters.ty + parameters.rz * point.x + scale * point.y - parameters.rx * point.z,
            z: parameters.tz - parameters.ry * point.x + parameters.rx * point.y + scale * point.z,
        };
    }

    function meridionalArc(latitude) {
        const {a, b} = airy1830;
        const n = (a - b) / (a + b);
        const difference = latitude - originLatitude;
        const sum = latitude + originLatitude;
        return b * scaleFactor * (
            (1 + n + 1.25 * n ** 2 + 1.25 * n ** 3) * difference
            - (3 * n + 3 * n ** 2 + 2.625 * n ** 3) * Math.sin(difference) * Math.cos(sum)
            + (1.875 * n ** 2 + 1.875 * n ** 3) * Math.sin(2 * difference) * Math.cos(2 * sum)
            - (35 / 24) * n ** 3 * Math.sin(3 * difference) * Math.cos(3 * sum));
    }

    function curvatures(latitude) {
        const e2 = eccentricitySquared(airy1830);
        const denominator = 1 - e2 * Math.sin(latitude) ** 2;
        const nu = airy1830.a * scaleFactor / Math.sqrt(denominator);
        const rho = airy1830.a * scaleFactor * (1 - e2) / denominator ** 1.5;
        return {nu, rho, eta2: nu / rho - 1};
    }

    function wgs84ToGrid(latitude, longitude) {
        const wgs84 = toCartesian(latitude * degrees, longitude * degrees, 0, grs80);
        const osgb36 = toGeodetic(helmert(wgs84, toOsgb36), airy1830);
        const phi = osgb36.latitude;
        const {nu, rho, eta2} = curvatures(phi);
        const sine = Math.sin(phi);
        const cosine = Math.cos(phi);
        const tan2 = Math.tan(phi) ** 2;
        const delta = osgb36.longitude - originLongitude;
        const northing = meridionalArc(phi) + falseNorthing
            + nu / 2 * sine * cosine * delta ** 2
            + nu / 24 * sine * cosine ** 3 * (5 - tan2 + 9 * eta2) * delta ** 4
            + nu / 720 * sine * cosine ** 5 * (61 - 58 * tan2 + tan2 ** 2) * delta ** 6;
        const easting = falseEasting
            + nu * cosine * delta
            + nu / 6 * cosine ** 3 * (nu / rho - tan2) * delta ** 3
            + nu / 120 * cosine ** 5 * (5 - 18 * tan2 + tan2 ** 2 + 14 * eta2 - 58 * tan2 * eta2) * delta ** 5;
        return {east: easting, north: northing};
    }

    function gridToWgs84(east, north) {
        let phi = originLatitude;
        let arc = 0;
        do {
            phi += (north - falseNorthing - arc) / (airy1830.a * scaleFactor);
            arc = meridionalArc(phi);
        } while (Math.abs(north - falseNorthing - arc) >= 0.00001);
        const {nu, rho, eta2} = curvatures(phi);
        const tangent = Math.tan(phi);
        const secant = 1 / Math.cos(phi);
        const t2 = tangent ** 2;
        const offset = east - falseEasting;
        const latitude = phi
            - tangent / (2 * rho * nu) * offset ** 2
            + tangent / (24 * rho * nu ** 3) * (5 + 3 * t2 + eta2 - 9 * t2 * eta2) * offset ** 4
            - tangent / (720 * rho * nu ** 5) * (61 + 90 * t2 + 45 * t2 ** 2) * offset ** 6;
        const longitude = originLongitude
            + secant / nu * offset
            - secant / (6 * nu ** 3) * (nu / rho + 2 * t2) * offset ** 3
            + secant / (120 * nu ** 5) * (5 + 28 * t2 + 24 * t2 ** 2) * offset ** 5
            - secant / (5040 * nu ** 7) * (61 + 662 * t2 + 1320 * t2 ** 2 + 720 * t2 ** 3) * offset ** 7;
        const osgb36 = toCartesian(latitude, longitude, 0, airy1830);
        const wgs84 = toGeodetic(helmert(osgb36, toWgs84), grs80);
        return {latitude: wgs84.latitude / degrees, longitude: wgs84.longitude / degrees};
    }

    function gridReference(east, north, digits = 6) {
        if (east < 0 || north < 0 || east >= 700000 || north >= 1300000) {
            return "";
        }
        const eastSquare = Math.floor(east / 100000);
        const northSquare = Math.floor(north / 100000);
        const first = (19 - northSquare) - (19 - northSquare) % 5 + Math.floor((eastSquare + 10) / 5);
        const second = (19 - northSquare) * 5 % 25 + eastSquare % 5;
        const half = digits / 2;
        const divisor = 10 ** (5 - half);
        const pad = value => String(Math.floor((value % 100000) / divisor)).padStart(half, "0");
        return `${gridLetters[first]}${gridLetters[second]} ${pad(east)} ${pad(north)}`;
    }

    const tasks = ["visit", "photo", "choice", "count", "via"];

    function unescapeValue(value) {
        return value.replace(/\\n/g, "\n");
    }

    function escapeValue(value) {
        return String(value).replace(/\r?\n/g, "\\n").trim();
    }

    function parseMission(text) {
        const mission = {title: "", intro: "", waypoints: []};
        const answers = [];
        let current = null;
        const lines = text.replace(/^\uFEFF/, "").split("\n");
        lines.forEach((raw, index) => {
            const lineNumber = index + 1;
            const line = raw.trim();
            if (!line || line.startsWith("#") || line.startsWith(";")) {
                return;
            }
            if (line.startsWith("[")) {
                const close = line.indexOf("]");
                if (close < 0) {
                    throw new Error(`line ${lineNumber}: missing ]`);
                }
                current = {name: unescapeValue(line.slice(1, close).trim()), latitude: null, longitude: null, radius: 25, task: "visit", route: false, clue: "", prompt: "", choices: [], answer: "", photo: ""};
                mission.waypoints.push(current);
                answers.push("");
                return;
            }
            const equals = line.indexOf("=");
            if (equals < 0) {
                throw new Error(`line ${lineNumber}: expected key = value`);
            }
            const key = line.slice(0, equals).trim().toLowerCase();
            const value = line.slice(equals + 1).trim();
            if (!current) {
                if (key === "title" || key === "intro") {
                    mission[key] = unescapeValue(value);
                    return;
                }
                throw new Error(`line ${lineNumber}: unknown mission key '${key}'`);
            }
            switch (key) {
            case "location": {
                const parts = value.split(",").map(part => Number(part.trim()));
                if (parts.length !== 2 || parts.some(Number.isNaN) || Math.abs(parts[0]) > 90 || Math.abs(parts[1]) > 180) {
                    throw new Error(`line ${lineNumber}: location should be latitude, longitude`);
                }
                [current.latitude, current.longitude] = parts;
                break;
            }
            case "radius": {
                const radius = Number(value);
                if (!(radius >= 1)) {
                    throw new Error(`line ${lineNumber}: radius should be a number of metres`);
                }
                current.radius = Math.round(radius);
                break;
            }
            case "task":
                if (!tasks.includes(value.toLowerCase())) {
                    throw new Error(`line ${lineNumber}: task should be visit, photo, choice, count or via`);
                }
                current.task = value.toLowerCase();
                break;
            case "route": {
                const flag = value.toLowerCase();
                if (!["yes", "true", "no", "false"].includes(flag)) {
                    throw new Error(`line ${lineNumber}: route should be yes or no`);
                }
                current.route = flag === "yes" || flag === "true";
                break;
            }
            case "clue":
            case "prompt":
            case "photo":
                current[key] = unescapeValue(value);
                break;
            case "choices":
                current.choices = value.split("|").map(choice => unescapeValue(choice.trim())).filter(Boolean);
                break;
            case "answer":
                answers[answers.length - 1] = unescapeValue(value);
                break;
            default:
                throw new Error(`line ${lineNumber}: unknown waypoint key '${key}'`);
            }
        });
        if (!mission.waypoints.length) {
            throw new Error("no waypoints");
        }
        mission.waypoints.forEach((waypoint, index) => {
            if (!waypoint.name && waypoint.task !== "via") {
                throw new Error(`waypoint ${index + 1} needs a name`);
            }
            if (waypoint.latitude === null) {
                throw new Error(`${waypoint.name}: no location`);
            }
            if (waypoint.task === "choice") {
                if (waypoint.choices.length < 2) {
                    throw new Error(`${waypoint.name}: a choice task needs at least two choices`);
                }
                const answer = answers[index];
                const match = waypoint.choices.find(choice => choice.toLowerCase() === answer.toLowerCase());
                if (answer && !match) {
                    throw new Error(`${waypoint.name}: answer '${answer}' isn't one of the choices`);
                }
                waypoint.answer = match || "";
            }
        });
        return mission;
    }

    function missionProblems(mission) {
        const problems = [];
        if (!mission.waypoints.length) {
            problems.push("Add at least one waypoint.");
        }
        mission.waypoints.forEach((waypoint, index) => {
            const label = waypoint.name || `Waypoint ${index + 1}`;
            if (!waypoint.name.trim() && waypoint.task !== "via") {
                problems.push(`Waypoint ${index + 1} needs a name.`);
            }
            if (/[\[\]]/.test(waypoint.name)) {
                problems.push(`${label}: names can't contain [ or ].`);
            }
            if (waypoint.task === "choice") {
                if (waypoint.choices.length < 2) {
                    problems.push(`${label}: a choice task needs at least two choices.`);
                }
                if (waypoint.choices.some(choice => choice.includes("|"))) {
                    problems.push(`${label}: choices can't contain |.`);
                }
            }
        });
        return problems;
    }

    function writeMission(mission) {
        const lines = [
            "# GISmo mission. Lines starting with # are comments.",
            "# Each [section] is a waypoint, visited in order. location is latitude, longitude (WGS84).",
            "# task is visit, photo, choice (pick from choices, answer is the right one), count (enter a number)",
            "# or via (a point on the way, passed without stopping; its name is optional).",
            "# radius is how close, in metres, counts as arriving (default 25). photo is an optional hint picture.",
            "# route = yes shows the walking route to that waypoint on the map.",
            "",
            `title = ${escapeValue(mission.title)}`,
        ];
        if (mission.intro.trim()) {
            lines.push(`intro = ${escapeValue(mission.intro)}`);
        }
        for (const waypoint of mission.waypoints) {
            lines.push("", `[${escapeValue(waypoint.name)}]`, `location = ${waypoint.latitude.toFixed(6)}, ${waypoint.longitude.toFixed(6)}`);
            if (waypoint.radius !== 25) {
                lines.push(`radius = ${waypoint.radius}`);
            }
            lines.push(`task = ${waypoint.task}`);
            if (waypoint.route) {
                lines.push("route = yes");
            }
            if (waypoint.task === "via") {
                continue;
            }
            for (const key of ["clue", "prompt"]) {
                if (waypoint[key].trim()) {
                    lines.push(`${key} = ${escapeValue(waypoint[key])}`);
                }
            }
            if (waypoint.task === "choice") {
                lines.push(`choices = ${waypoint.choices.map(escapeValue).join(" | ")}`);
                if (waypoint.answer) {
                    lines.push(`answer = ${escapeValue(waypoint.answer)}`);
                }
            }
            if (waypoint.photo) {
                lines.push(`photo = ${escapeValue(waypoint.photo)}`);
            }
        }
        return lines.join("\n") + "\n";
    }

    const tileSize = 256;

    function expand565(value) {
        const red = value >> 11;
        const green = (value >> 5) & 0x3F;
        const blue = value & 0x1F;
        return [(red << 3) | (red >> 2), (green << 2) | (green >> 4), (blue << 3) | (blue >> 2)];
    }

    async function inflate(bytes) {
        const stream = new Blob([bytes]).stream().pipeThrough(new DecompressionStream("deflate"));
        return new Uint8Array(await new Response(stream).arrayBuffer());
    }

    class RouteGraph {
        constructor(buffer, fileName) {
            const view = new DataView(buffer);
            const magic = String.fromCharCode(...new Uint8Array(buffer, 0, 8));
            if (magic !== "GISMORTE" || view.getUint16(8, true) !== 1) {
                throw new Error(`${fileName} isn't a GISmo route graph`);
            }
            this.fileName = fileName;
            this.nodeCount = view.getUint32(12, true);
            const edgeCount = view.getUint32(16, true);
            const nodesOffset = 20;
            const offsetsOffset = nodesOffset + this.nodeCount * 8;
            const edgesOffset = offsetsOffset + (this.nodeCount + 1) * 4;
            this.nodes = new Int32Array(buffer, nodesOffset, this.nodeCount * 2);
            this.offsets = new Uint32Array(buffer, offsetsOffset, this.nodeCount + 1);
            this.edges = new Uint32Array(buffer, edgesOffset, edgeCount * 2);
            let west = Infinity;
            let south = Infinity;
            let east = -Infinity;
            let north = -Infinity;
            for (let node = 0; node < this.nodeCount; node++) {
                west = Math.min(west, this.nodes[node * 2]);
                east = Math.max(east, this.nodes[node * 2]);
                south = Math.min(south, this.nodes[node * 2 + 1]);
                north = Math.max(north, this.nodes[node * 2 + 1]);
            }
            this.bounds = {west: west / 10, south: south / 10, east: east / 10, north: north / 10};
        }

        covers(east, north) {
            return east >= this.bounds.west && east <= this.bounds.east && north >= this.bounds.south && north <= this.bounds.north;
        }

        distanceTo(node, point) {
            return Math.hypot(this.nodes[node * 2] / 10 - point.east, this.nodes[node * 2 + 1] / 10 - point.north);
        }

        nearest(east, north) {
            const targetEast = Math.round(east * 10);
            const targetNorth = Math.round(north * 10);
            let best = -1;
            let bestDistance = Infinity;
            for (let node = 0; node < this.nodeCount; node++) {
                const distance = (this.nodes[node * 2] - targetEast) ** 2 + (this.nodes[node * 2 + 1] - targetNorth) ** 2;
                if (distance < bestDistance) {
                    best = node;
                    bestDistance = distance;
                }
            }
            return best;
        }

        find(from, to) {
            const goalEast = this.nodes[to * 2];
            const goalNorth = this.nodes[to * 2 + 1];
            const heuristic = node => Math.floor(Math.hypot(this.nodes[node * 2] - goalEast, this.nodes[node * 2 + 1] - goalNorth));
            const best = new Map([[from, 0]]);
            const parent = new Map();
            const closed = new Set();
            const heap = [[heuristic(from), from]];
            const push = entry => {
                heap.push(entry);
                let index = heap.length - 1;
                while (index > 0) {
                    const up = (index - 1) >> 1;
                    if (heap[up][0] <= heap[index][0]) {
                        break;
                    }
                    [heap[up], heap[index]] = [heap[index], heap[up]];
                    index = up;
                }
            };
            const pop = () => {
                const top = heap[0];
                const last = heap.pop();
                if (heap.length) {
                    heap[0] = last;
                    let index = 0;
                    for (;;) {
                        const left = index * 2 + 1;
                        const right = left + 1;
                        let smallest = index;
                        if (left < heap.length && heap[left][0] < heap[smallest][0]) {
                            smallest = left;
                        }
                        if (right < heap.length && heap[right][0] < heap[smallest][0]) {
                            smallest = right;
                        }
                        if (smallest === index) {
                            break;
                        }
                        [heap[smallest], heap[index]] = [heap[index], heap[smallest]];
                        index = smallest;
                    }
                }
                return top;
            };
            while (heap.length) {
                const [, node] = pop();
                if (node === to) {
                    const path = [to];
                    while (path[path.length - 1] !== from) {
                        path.push(parent.get(path[path.length - 1]));
                    }
                    path.reverse();
                    return {cost: best.get(to), points: path.map(index => ({east: this.nodes[index * 2] / 10, north: this.nodes[index * 2 + 1] / 10}))};
                }
                if (closed.has(node)) {
                    continue;
                }
                closed.add(node);
                const cost = best.get(node);
                for (let edge = this.offsets[node]; edge < this.offsets[node + 1]; edge++) {
                    const target = this.edges[edge * 2];
                    const candidate = cost + this.edges[edge * 2 + 1];
                    if (candidate < (best.get(target) ?? Infinity)) {
                        best.set(target, candidate);
                        parent.set(target, node);
                        closed.delete(target);
                        push([candidate + heuristic(target), target]);
                    }
                }
            }
            return null;
        }
    }

    class MapPack {
        constructor(buffer, fileName) {
            const view = new DataView(buffer);
            const magic = new TextDecoder().decode(new Uint8Array(buffer, 0, 8));
            if (magic !== "GISMOMAP" || view.getUint16(8, true) !== 1 || view.getUint16(10, true) !== tileSize) {
                throw new Error(`${fileName} isn't a GISmo map pack`);
            }
            this.buffer = buffer;
            this.fileName = fileName;
            const levelCount = view.getUint16(12, true);
            const levelTable = view.getUint32(16, true);
            const textOffset = view.getUint32(20, true);
            const textLength = view.getUint32(24, true);
            const text = new TextDecoder().decode(new Uint8Array(buffer, textOffset, textLength));
            this.properties = Object.fromEntries(text.split("\n").filter(line => line.includes("=")).map(line => [line.slice(0, line.indexOf("=")), line.slice(line.indexOf("=") + 1)]));
            this.name = this.properties.name || fileName;
            this.levels = [];
            for (let index = 0; index < levelCount; index++) {
                const record = levelTable + index * 32;
                const millimetresPerPixel = view.getUint32(record, true);
                const columns = view.getUint32(record + 12, true);
                const rows = view.getUint32(record + 16, true);
                const indexOffset = view.getUint32(record + 20, true);
                this.levels.push({
                    metresPerPixel: millimetresPerPixel / 1000,
                    span: millimetresPerPixel * tileSize / 1000,
                    west: view.getInt32(record + 4, true),
                    north: view.getInt32(record + 8, true),
                    columns,
                    rows,
                    index: new Uint32Array(buffer.slice(indexOffset, indexOffset + columns * rows * 8)),
                });
            }
            this.cache = new Map();
        }

        static async open(file) {
            return new MapPack(await file.arrayBuffer(), file.name);
        }

        get finest() {
            return this.levels[0].metresPerPixel;
        }

        get coarsest() {
            return this.levels[this.levels.length - 1].metresPerPixel;
        }

        bounds() {
            const level = this.levels[0];
            return {west: level.west, north: level.north, east: level.west + level.columns * level.span, south: level.north - level.rows * level.span};
        }

        bestLevel(metresPerPixel) {
            let best = 0;
            let bestDistance = Infinity;
            this.levels.forEach((level, index) => {
                const distance = Math.abs(Math.log(level.metresPerPixel / metresPerPixel));
                if (distance < bestDistance - 1e-6) {
                    best = index;
                    bestDistance = distance;
                }
            });
            return best;
        }

        locate(levelIndex, column, row) {
            const level = this.levels[levelIndex];
            const indexColumn = column - level.west / level.span;
            const indexRow = level.north / level.span - 1 - row;
            if (indexColumn < 0 || indexRow < 0 || indexColumn >= level.columns || indexRow >= level.rows) {
                return null;
            }
            const entry = (indexRow * level.columns + indexColumn) * 2;
            const length = level.index[entry + 1];
            return length ? {offset: level.index[entry], length} : null;
        }

        async decode(levelIndex, column, row) {
            const located = this.locate(levelIndex, column, row);
            if (!located) {
                return null;
            }
            const bytes = new Uint8Array(this.buffer, located.offset, located.length);
            const transparent = (bytes[0] & 1) !== 0;
            const count = bytes[1] + 1;
            const palette = [];
            for (let index = 0; index < count; index++) {
                palette.push(expand565(bytes[2 + index * 2] | bytes[3 + index * 2] << 8));
            }
            const indices = count > 1 ? await inflate(bytes.subarray(2 + count * 2)) : new Uint8Array(tileSize * tileSize);
            const rgba = new Uint8ClampedArray(tileSize * tileSize * 4);
            for (let pixel = 0; pixel < tileSize * tileSize; pixel++) {
                const index = indices[pixel];
                const [red, green, blue] = palette[index];
                rgba[pixel * 4] = red;
                rgba[pixel * 4 + 1] = green;
                rgba[pixel * 4 + 2] = blue;
                rgba[pixel * 4 + 3] = transparent && index === 0 ? 0 : 255;
            }
            return {width: tileSize, height: tileSize, rgba};
        }
    }

    function bmp565(rgba, width, height) {
        const rowBytes = (width * 2 + 3) & ~3;
        const headerSize = 66;
        const buffer = new ArrayBuffer(headerSize + rowBytes * height);
        const view = new DataView(buffer);
        view.setUint8(0, 0x42);
        view.setUint8(1, 0x4D);
        view.setUint32(2, buffer.byteLength, true);
        view.setUint32(10, headerSize, true);
        view.setUint32(14, 40, true);
        view.setInt32(18, width, true);
        view.setInt32(22, height, true);
        view.setUint16(26, 1, true);
        view.setUint16(28, 16, true);
        view.setUint32(30, 3, true);
        view.setUint32(34, rowBytes * height, true);
        view.setInt32(38, 2835, true);
        view.setInt32(42, 2835, true);
        view.setUint32(54, 0xF800, true);
        view.setUint32(58, 0x07E0, true);
        view.setUint32(62, 0x001F, true);
        for (let y = 0; y < height; y++) {
            const row = headerSize + (height - 1 - y) * rowBytes;
            for (let x = 0; x < width; x++) {
                const source = (y * width + x) * 4;
                const value = (rgba[source] >> 3) << 11 | (rgba[source + 1] >> 2) << 5 | rgba[source + 2] >> 3;
                view.setUint16(row + x * 2, value, true);
            }
        }
        return new Uint8Array(buffer);
    }

    function slug(text) {
        return text.toLowerCase().normalize("NFKD").replace(/[^\w\s-]/g, "").trim().replace(/[\s_]+/g, "-").replace(/-+/g, "-").slice(0, 40) || "mission";
    }

    const crcTable = Array.from({length: 256}, (_, index) => {
        let value = index;
        for (let bit = 0; bit < 8; bit++) {
            value = value & 1 ? 0xEDB88320 ^ (value >>> 1) : value >>> 1;
        }
        return value >>> 0;
    });

    function crc32(bytes) {
        let crc = 0xFFFFFFFF;
        for (const byte of bytes) {
            crc = crcTable[(crc ^ byte) & 0xFF] ^ (crc >>> 8);
        }
        return (crc ^ 0xFFFFFFFF) >>> 0;
    }

    function zip(files, date = new Date()) {
        const dosTime = date.getHours() << 11 | date.getMinutes() << 5 | Math.floor(date.getSeconds() / 2);
        const dosDate = (date.getFullYear() - 1980) << 9 | (date.getMonth() + 1) << 5 | date.getDate();
        const encoder = new TextEncoder();
        const parts = [];
        const directory = [];
        let offset = 0;
        for (const file of files) {
            const name = encoder.encode(file.name);
            const bytes = typeof file.bytes === "string" ? encoder.encode(file.bytes) : file.bytes;
            const crc = crc32(bytes);
            const local = new DataView(new ArrayBuffer(30));
            local.setUint32(0, 0x04034B50, true);
            local.setUint16(4, 20, true);
            local.setUint16(6, 0x0800, true);
            local.setUint16(10, dosTime, true);
            local.setUint16(12, dosDate, true);
            local.setUint32(14, crc, true);
            local.setUint32(18, bytes.length, true);
            local.setUint32(22, bytes.length, true);
            local.setUint16(26, name.length, true);
            parts.push(new Uint8Array(local.buffer), name, bytes);
            const central = new DataView(new ArrayBuffer(46));
            central.setUint32(0, 0x02014B50, true);
            central.setUint16(4, 20, true);
            central.setUint16(6, 20, true);
            central.setUint16(8, 0x0800, true);
            central.setUint16(12, dosTime, true);
            central.setUint16(14, dosDate, true);
            central.setUint32(16, crc, true);
            central.setUint32(20, bytes.length, true);
            central.setUint32(24, bytes.length, true);
            central.setUint16(28, name.length, true);
            central.setUint32(42, offset, true);
            directory.push(new Uint8Array(central.buffer), name);
            offset += 30 + name.length + bytes.length;
        }
        const directorySize = directory.reduce((total, part) => total + part.length, 0);
        const end = new DataView(new ArrayBuffer(22));
        end.setUint32(0, 0x06054B50, true);
        end.setUint16(8, files.length, true);
        end.setUint16(10, files.length, true);
        end.setUint32(12, directorySize, true);
        end.setUint32(16, offset, true);
        const all = [...parts, ...directory, new Uint8Array(end.buffer)];
        const output = new Uint8Array(all.reduce((total, part) => total + part.length, 0));
        let position = 0;
        for (const part of all) {
            output.set(part, position);
            position += part.length;
        }
        return output;
    }

    async function unzip(buffer) {
        const bytes = new Uint8Array(buffer);
        const view = new DataView(buffer);
        let end = bytes.length - 22;
        while (end >= 0 && view.getUint32(end, true) !== 0x06054B50) {
            end--;
        }
        if (end < 0) {
            throw new Error("not a zip file");
        }
        const count = view.getUint16(end + 10, true);
        let position = view.getUint32(end + 16, true);
        const decoder = new TextDecoder();
        const files = new Map();
        for (let entry = 0; entry < count; entry++) {
            if (view.getUint32(position, true) !== 0x02014B50) {
                throw new Error("damaged zip directory");
            }
            const method = view.getUint16(position + 10, true);
            const compressed = view.getUint32(position + 20, true);
            const nameLength = view.getUint16(position + 28, true);
            const extraLength = view.getUint16(position + 30, true);
            const commentLength = view.getUint16(position + 32, true);
            const offset = view.getUint32(position + 42, true);
            const name = decoder.decode(bytes.subarray(position + 46, position + 46 + nameLength));
            position += 46 + nameLength + extraLength + commentLength;
            if (name.endsWith("/")) {
                continue;
            }
            const start = offset + 30 + view.getUint16(offset + 26, true) + view.getUint16(offset + 28, true);
            const data = bytes.subarray(start, start + compressed);
            if (method === 0) {
                files.set(name, data);
            } else if (method === 8) {
                const stream = new Blob([data]).stream().pipeThrough(new DecompressionStream("deflate-raw"));
                files.set(name, new Uint8Array(await new Response(stream).arrayBuffer()));
            } else {
                throw new Error(`${name} uses an unsupported zip compression method`);
            }
        }
        return files;
    }

    function exifPosition(bytes) {
        const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
        if (view.getUint16(0) !== 0xFFD8) {
            return null;
        }
        let position = 2;
        while (position + 4 < bytes.length && bytes[position] === 0xFF) {
            const marker = bytes[position + 1];
            const length = view.getUint16(position + 2);
            if (marker === 0xE1 && String.fromCharCode(...bytes.subarray(position + 4, position + 8)) === "Exif") {
                return readExif(view, position + 10);
            }
            if (marker === 0xDA) {
                break;
            }
            position += 2 + length;
        }
        return null;
    }

    function readExif(view, tiff) {
        const little = view.getUint16(tiff) === 0x4949;
        const u16 = offset => view.getUint16(tiff + offset, little);
        const u32 = offset => view.getUint32(tiff + offset, little);
        const entries = ifd => {
            const found = new Map();
            const count = u16(ifd);
            for (let index = 0; index < count; index++) {
                const entry = ifd + 2 + index * 12;
                found.set(u16(entry), {type: u16(entry + 2), count: u32(entry + 4), value: entry + 8});
            }
            return found;
        };
        const ascii = field => {
            const offset = field.count > 4 ? u32(field.value) : field.value;
            let text = "";
            for (let index = 0; index < field.count - 1; index++) {
                text += String.fromCharCode(view.getUint8(tiff + offset + index));
            }
            return text;
        };
        const rationals = field => {
            const offset = u32(field.value);
            return Array.from({length: field.count}, (_, index) => u32(offset + index * 8) / u32(offset + index * 8 + 4));
        };
        const top = entries(u32(4));
        const gpsPointer = top.get(0x8825);
        if (!gpsPointer) {
            return null;
        }
        const gps = entries(u32(gpsPointer.value));
        if (!gps.has(2) || !gps.has(4)) {
            return null;
        }
        const degrees = values => values[0] + values[1] / 60 + values[2] / 3600;
        const latitude = degrees(rationals(gps.get(2))) * (gps.has(1) && ascii(gps.get(1)) === "S" ? -1 : 1);
        const longitude = degrees(rationals(gps.get(4))) * (gps.has(3) && ascii(gps.get(3)) === "W" ? -1 : 1);
        const result = {latitude, longitude, altitude: null, time: null};
        if (gps.has(6)) {
            result.altitude = rationals(gps.get(6))[0] * (gps.has(5) && view.getUint8(tiff + gps.get(5).value) === 1 ? -1 : 1);
        }
        if (gps.has(29) && gps.has(7)) {
            const [hour, minute, second] = rationals(gps.get(7));
            const date = ascii(gps.get(29)).replace(/:/g, "-");
            result.time = Date.parse(`${date}T${String(hour).padStart(2, "0")}:${String(minute).padStart(2, "0")}:${String(Math.floor(second)).padStart(2, "0")}Z`);
        }
        return result;
    }

    function parseGpx(text) {
        const points = [];
        for (const match of text.matchAll(/<trkpt\b([^>]*)>([\s\S]*?)<\/trkpt>/g)) {
            const latitude = Number(/\blat="([^"]+)"/.exec(match[1])?.[1]);
            const longitude = Number(/\blon="([^"]+)"/.exec(match[1])?.[1]);
            if (Number.isNaN(latitude) || Number.isNaN(longitude)) {
                continue;
            }
            const time = /<time>([^<]+)<\/time>/.exec(match[2])?.[1];
            const elevation = /<ele>([^<]+)<\/ele>/.exec(match[2])?.[1];
            points.push({latitude, longitude, time: time ? Date.parse(time) : null, elevation: elevation === undefined ? null : Number(elevation)});
        }
        return points;
    }

    function parseCsv(text) {
        const rows = [];
        let row = [];
        let field = "";
        let quoted = false;
        for (let index = 0; index < text.length; index++) {
            const character = text[index];
            if (quoted) {
                if (character === '"' && text[index + 1] === '"') {
                    field += '"';
                    index++;
                } else if (character === '"') {
                    quoted = false;
                } else {
                    field += character;
                }
            } else if (character === '"') {
                quoted = true;
            } else if (character === ",") {
                row.push(field);
                field = "";
            } else if (character === "\n") {
                row.push(field.replace(/\r$/, ""));
                rows.push(row);
                row = [];
                field = "";
            } else {
                field += character;
            }
        }
        if (field || row.length) {
            row.push(field);
            rows.push(row);
        }
        return rows;
    }

    function trackStats(segments) {
        let metres = 0;
        let first = null;
        let last = null;
        for (const points of segments) {
            const grid = points.map(point => wgs84ToGrid(point.latitude, point.longitude));
            for (let index = 1; index < grid.length; index++) {
                metres += Math.hypot(grid[index].east - grid[index - 1].east, grid[index].north - grid[index - 1].north);
            }
            for (const point of points) {
                if (point.time !== null && !Number.isNaN(point.time)) {
                    first = first === null ? point.time : Math.min(first, point.time);
                    last = last === null ? point.time : Math.max(last, point.time);
                }
            }
        }
        return {metres, seconds: first === null ? 0 : (last - first) / 1000, start: first, end: last};
    }

    return {wgs84ToGrid, gridToWgs84, gridReference, parseMission, writeMission, missionProblems, MapPack, RouteGraph, bmp565, slug, tasks, tileSize, zip, unzip, exifPosition, parseGpx, parseCsv, trackStats};
})();

if (typeof module !== "undefined") {
    module.exports = gismo;
}
