"use strict";

const thumbWidth = 44;
const thumbHeight = 33;
const waypointRadius = 10;
const viaRadius = 5;

const eventLabels = {
    start: "Started logging",
    stop: "Stopped logging",
    fix: "Got a GPS fix",
    target: "Heading for",
    arrived: "Arrived at",
    answer: "Answered at",
    completed: "Completed",
    photo: "Took a photo",
    "photo hint": "Looked at the photo hint for",
    passed: "Passed a route point",
    "mission complete": "Finished the mission",
    "explore target": "Exploring towards",
    visited: "Found",
    "outing saved": "Saved the outing",
    "outing save failed": "Couldn't save the outing",
    mission: "Chose the mission",
};

const state = {
    title: "",
    segments: [],
    events: [],
    photos: [],
    mission: null,
    summaryText: "",
    selected: -1,
};

const canvas = document.querySelector("figure canvas");
const heading = document.querySelector("aside h2");
const intro = document.querySelector("aside > p");
const statsList = document.querySelector("aside dl");
const timeline = document.querySelector("aside ol");
const problems = document.querySelector("[role=status]");
const summaryOutput = document.querySelector("output[name=summary]");
const cursorOutput = document.querySelector("output[name=cursor]");
const attribution = document.querySelector("figcaption small");
const layerSelect = document.querySelector("select[name=layer]");
const dialog = document.querySelector("dialog");

const mapView = new MapView(canvas, {
    emptyMessage: "Open an outing, then add its .gmap maps or tick OpenStreetMap",
    draw: drawOuting,
    hit: (x, y) => {
        const index = photoAt(x, y);
        return index >= 0 ? index : null;
    },
    click: (x, y, item) => {
        if (item !== null) {
            showPhoto(item);
        }
    },
    hover: (x, y, grid) => {
        const position = gismo.gridToWgs84(grid.east, grid.north);
        cursorOutput.value = `${gismo.gridReference(grid.east, grid.north, 8)}   ${position.latitude.toFixed(5)}, ${position.longitude.toFixed(5)}`;
        canvas.style.cursor = photoAt(x, y) >= 0 ? "pointer" : "";
    },
});

function grid(latitude, longitude) {
    return gismo.wgs84ToGrid(latitude, longitude);
}

function photoAt(x, y) {
    for (let index = state.photos.length - 1; index >= 0; index--) {
        const photo = state.photos[index];
        if (!photo.grid) {
            continue;
        }
        const point = mapView.toScreen(photo.grid.east, photo.grid.north);
        if (Math.abs(x - point.x) <= thumbWidth / 2 + 2 && Math.abs(y - point.y) <= thumbHeight / 2 + 2) {
            return index;
        }
    }
    return -1;
}

function drawOuting(context) {
    for (const points of state.segments) {
        if (points.length < 2) {
            continue;
        }
        for (const [width, colour] of [[7, "rgb(255 255 255 / 0.9)"], [3.5, "#0a6cff"]]) {
            context.beginPath();
            points.forEach((point, index) => {
                const screen = mapView.toScreen(point.grid.east, point.grid.north);
                index ? context.lineTo(screen.x, screen.y) : context.moveTo(screen.x, screen.y);
            });
            context.lineWidth = width;
            context.lineJoin = "round";
            context.lineCap = "round";
            context.strokeStyle = colour;
            context.stroke();
        }
    }
    const allPoints = state.segments.flat();
    if (allPoints.length) {
        drawDot(context, allPoints[0].grid, 7, "#28aa46");
        drawDot(context, allPoints[allPoints.length - 1].grid, 7, "#d23c28");
    }
    drawWaypoints(context);
    state.photos.forEach((photo, index) => {
        if (!photo.grid || !photo.bitmap) {
            return;
        }
        const point = mapView.toScreen(photo.grid.east, photo.grid.north);
        const selected = state.events[state.selected]?.photo === photo;
        context.fillStyle = selected ? "#ff7a00" : "white";
        context.fillRect(point.x - thumbWidth / 2 - 2, point.y - thumbHeight / 2 - 2, thumbWidth + 4, thumbHeight + 4);
        context.drawImage(photo.bitmap, point.x - thumbWidth / 2, point.y - thumbHeight / 2, thumbWidth, thumbHeight);
    });
    const event = state.events[state.selected];
    if (event?.grid && !event.photo) {
        drawDot(context, event.grid, 9, "#ff7a00");
    }
}

function drawDot(context, position, radius, colour) {
    const point = mapView.toScreen(position.east, position.north);
    context.beginPath();
    context.arc(point.x, point.y, radius, 0, Math.PI * 2);
    context.fillStyle = colour;
    context.strokeStyle = "white";
    context.lineWidth = 2.5;
    context.fill();
    context.stroke();
}

function drawWaypoints(context) {
    if (!state.mission) {
        return;
    }
    context.font = "bold 11px system-ui, sans-serif";
    context.textAlign = "center";
    context.textBaseline = "middle";
    let stop = 0;
    for (const waypoint of state.mission.waypoints) {
        const via = waypoint.task === "via";
        stop += via ? 0 : 1;
        const position = grid(waypoint.latitude, waypoint.longitude);
        drawDot(context, position, via ? viaRadius : waypointRadius, waypoint.done ? "#28aa46" : "#7c3cc8");
        if (!via) {
            const point = mapView.toScreen(position.east, position.north);
            context.fillStyle = "white";
            context.fillText(String(stop), point.x, point.y + 0.5);
        }
    }
    context.textAlign = "start";
}

function clock(time) {
    if (time === null || Number.isNaN(time)) {
        return "";
    }
    return new Date(time).toISOString().slice(11, 16);
}

function formatDistance(metres) {
    return metres < 1000 ? `${Math.round(metres)} m` : `${(metres / 1000).toFixed(1)} km`;
}

function formatDuration(seconds) {
    const minutes = Math.round(seconds / 60);
    return minutes < 60 ? `${minutes} min` : `${Math.floor(minutes / 60)} h ${String(minutes % 60).padStart(2, "0")} min`;
}

function eventText(event) {
    const label = eventLabels[event.event] || event.event;
    if (event.event === "answer") {
        return `Answered "${event.detail}" at ${event.waypoint}`;
    }
    if (event.event === "photo") {
        return event.waypoint ? `Took a photo at ${event.waypoint}` : "Took a photo";
    }
    if (event.event === "mission") {
        return event.detail ? `${label} ${event.detail}` : label;
    }
    if (["start", "stop", "fix", "outing saved", "outing save failed", "mission complete", "passed"].includes(event.event)) {
        return label;
    }
    return event.waypoint ? `${label} ${event.waypoint}` : label;
}

function applyProgress(mission, text) {
    let current = null;
    for (const raw of text.split("\n")) {
        const line = raw.trim();
        const section = /^\[(.*)\]$/.exec(line);
        if (section) {
            current = mission.waypoints.find(waypoint => waypoint.name === section[1]) || null;
        } else if (current && /^done\s*=/.test(line)) {
            current.done = true;
        }
    }
}

async function addPhoto(name, bytes) {
    const blob = new Blob([bytes], {type: "image/jpeg"});
    const position = gismo.exifPosition(bytes);
    let bitmap = null;
    try {
        bitmap = await createImageBitmap(blob, {resizeWidth: thumbWidth * 2, resizeHeight: thumbHeight * 2, resizeQuality: "medium"});
    } catch {
        bitmap = null;
    }
    state.photos.push({
        name,
        url: URL.createObjectURL(blob),
        bitmap,
        latitude: position?.latitude ?? null,
        longitude: position?.longitude ?? null,
        grid: position ? grid(position.latitude, position.longitude) : null,
        time: position?.time ?? null,
    });
}

async function loadEntries(entries) {
    for (const photo of state.photos) {
        URL.revokeObjectURL(photo.url);
    }
    Object.assign(state, {title: "", segments: [], events: [], photos: [], mission: null, summaryText: "", selected: -1});
    const problemsFound = [];
    const decoder = new TextDecoder();
    let progressText = "";
    for (const {name, bytes} of entries) {
        const base = name.split("/").pop();
        const lower = base.toLowerCase();
        try {
            if (lower.endsWith(".zip")) {
                const files = await gismo.unzip(bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength));
                await loadEntries([...files].map(([inner, data]) => ({name: inner, bytes: data})).concat(entries.filter(entry => entry.name !== name)));
                return;
            } else if (lower.endsWith(".gpx")) {
                const points = gismo.parseGpx(decoder.decode(bytes)).map(point => ({...point, grid: grid(point.latitude, point.longitude)}));
                state.segments.push(points);
            } else if (lower === "events.csv") {
                const rows = gismo.parseCsv(decoder.decode(bytes)).slice(1);
                for (const [time, latitude, longitude, event, waypoint, detail] of rows) {
                    if (!event) {
                        continue;
                    }
                    const located = latitude !== "" && longitude !== "";
                    state.events.push({
                        time: time ? Date.parse(time) : null,
                        grid: located ? grid(Number(latitude), Number(longitude)) : null,
                        event,
                        waypoint: waypoint || "",
                        detail: detail || "",
                        photo: null,
                    });
                }
            } else if (lower === "summary.txt") {
                state.summaryText = decoder.decode(bytes);
                const title = /^GISmo outing: (.*)$/m.exec(state.summaryText);
                state.title = title ? title[1] : state.title;
            } else if (lower === "mission.txt") {
                state.mission = gismo.parseMission(decoder.decode(bytes));
            } else if (lower === "progress.txt") {
                progressText = decoder.decode(bytes);
            } else if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) {
                await addPhoto(base, bytes);
            }
        } catch (error) {
            problemsFound.push(`${base}: ${error.message}`);
        }
    }
    if (state.mission && progressText) {
        applyProgress(state.mission, progressText);
    }
    state.segments.sort((a, b) => (a[0]?.time ?? 0) - (b[0]?.time ?? 0));
    state.photos.sort((a, b) => (a.time ?? 0) - (b.time ?? 0) || a.name.localeCompare(b.name));
    linkPhotos();
    state.events.sort((a, b) => (a.time ?? 0) - (b.time ?? 0));
    if (!state.title) {
        state.title = state.mission?.title || (state.segments.length || state.events.length ? "Outing" : "");
    }
    problems.replaceChildren(...problemsFound.map(problem => Object.assign(document.createElement("li"), {textContent: problem})));
    renderSide();
    applyOnline();
    fitOuting();
}

function linkPhotos() {
    const byName = new Map(state.photos.map(photo => [photo.name, photo]));
    const linked = new Set();
    for (const event of state.events) {
        if (event.event !== "photo") {
            continue;
        }
        const name = event.detail.split(",")[0].trim();
        const photo = byName.get(name);
        if (photo) {
            event.photo = photo;
            linked.add(photo);
            if (!photo.grid && event.grid) {
                photo.grid = event.grid;
            }
        }
    }
    for (const photo of state.photos) {
        if (!linked.has(photo)) {
            state.events.push({time: photo.time, grid: photo.grid, event: "photo", waypoint: "", detail: photo.name, photo});
        }
    }
}

function renderSide() {
    heading.textContent = state.title || "No outing open";
    intro.hidden = Boolean(state.title);
    const stats = gismo.trackStats(state.segments);
    const rows = [];
    if (state.segments.length) {
        rows.push(["Distance", formatDistance(stats.metres)]);
        rows.push(["Time", formatDuration(stats.seconds)]);
        if (stats.seconds >= 60) {
            rows.push(["Average speed", `${(stats.metres / stats.seconds * 3.6).toFixed(1)} km/h`]);
        }
        if (stats.start !== null) {
            rows.push(["Date", new Date(stats.start).toISOString().slice(0, 10)]);
        }
    }
    if (state.mission) {
        const stops = state.mission.waypoints.filter(waypoint => waypoint.task !== "via");
        rows.push(["Stops found", `${stops.filter(waypoint => waypoint.done).length} of ${stops.length}`]);
    }
    if (state.photos.length) {
        rows.push(["Photos", String(state.photos.length)]);
    }
    statsList.replaceChildren(...rows.flatMap(([term, value]) => [
        Object.assign(document.createElement("dt"), {textContent: term}),
        Object.assign(document.createElement("dd"), {textContent: value}),
    ]));
    timeline.replaceChildren(...state.events.map((event, index) => {
        const item = document.createElement("li");
        const button = document.createElement("button");
        button.type = "button";
        const time = document.createElement("time");
        time.textContent = clock(event.time);
        if (event.time !== null && !Number.isNaN(event.time)) {
            time.dateTime = new Date(event.time).toISOString();
        }
        const description = document.createElement("span");
        description.textContent = eventText(event);
        if (event.photo) {
            const small = document.createElement("small");
            small.textContent = event.photo.name;
            description.append(small);
        }
        button.append(time, description);
        if (event.photo) {
            const image = document.createElement("img");
            image.src = event.photo.url;
            image.alt = eventText(event);
            button.append(image);
        }
        button.addEventListener("click", () => selectEvent(index));
        item.append(button);
        return item;
    }));
    summaryOutput.value = state.summaryText;
    summaryOutput.closest("details").hidden = !state.summaryText;
}

function selectEvent(index) {
    state.selected = index;
    for (const [position, item] of [...timeline.children].entries()) {
        item.setAttribute("aria-current", String(position === index));
    }
    const event = state.events[index];
    const position = event.photo?.grid || event.grid;
    if (position) {
        mapView.centreOn(position.east, position.north, Math.min(mapView.metresPerPixel(), 4));
    }
    if (event.photo) {
        showPhoto(state.photos.indexOf(event.photo));
    }
    mapView.scheduleRender();
}

function showPhoto(index) {
    const photo = state.photos[index];
    const event = state.events.find(candidate => candidate.photo === photo);
    dialog.querySelector("img").src = photo.url;
    const where = photo.latitude !== null ? `, ${photo.latitude.toFixed(5)}, ${photo.longitude.toFixed(5)}` : ", no position";
    dialog.querySelector("figcaption").textContent = `${event ? eventText(event) : photo.name}${photo.time ? ` at ${clock(photo.time)}` : ""}${where}`;
    dialog.showModal();
}

function tell(message) {
    dialog.querySelector("img").removeAttribute("src");
    dialog.querySelector("figcaption").textContent = message;
    dialog.showModal();
}

function fitOuting() {
    const points = state.segments.flat().map(point => point.grid);
    points.push(...state.photos.filter(photo => photo.grid).map(photo => photo.grid));
    if (state.mission) {
        points.push(...state.mission.waypoints.map(waypoint => grid(waypoint.latitude, waypoint.longitude)));
    }
    mapView.fit(points.length ? points : mapView.mapBounds());
}

function updateLayers() {
    layerSelect.replaceChildren(...mapView.details.map((pack, index) => new Option(pack.name, String(index))));
    layerSelect.value = String(mapView.layer);
    layerSelect.disabled = mapView.details.length < 2;
    const sentences = mapView.attributions().flatMap(note => note.split(/\.\s+/)).map(sentence => sentence.replace(/\.$/, "").trim()).filter(Boolean);
    attribution.textContent = sentences.length ? `${[...new Set(sentences)].join(". ")}.` : "";
}

async function filesToEntries(files) {
    return Promise.all([...files].map(async file => ({name: file.webkitRelativePath || file.name, bytes: new Uint8Array(await file.arrayBuffer())})));
}

async function collectFolder(folder, prefix, entries) {
    for await (const [name, handle] of folder.entries()) {
        if (name.startsWith(".")) {
            continue;
        }
        if (handle.kind === "directory") {
            if (name !== "thumbs" && name !== "view") {
                await collectFolder(handle, `${prefix}${name}/`, entries);
            }
        } else if (/\.(gpx|csv|jpe?g|txt)$/i.test(name)) {
            entries.push({name: `${prefix}${name}`, bytes: new Uint8Array(await (await handle.getFile()).arrayBuffer())});
        }
    }
}

async function openFolder() {
    if (!window.showDirectoryPicker) {
        tell(`Opening a folder needs Chrome or Edge. Choose the outing's files with "Outing zip or files" instead.`);
        return;
    }
    let folder;
    try {
        folder = await window.showDirectoryPicker({id: "gismo-session"});
    } catch {
        return;
    }
    const entries = [];
    await collectFolder(folder, "", entries);
    const sessions = new Set(entries.filter(entry => entry.name.endsWith("events.csv")).map(entry => entry.name.split("/").slice(0, -1).join("/")));
    if (sessions.size > 1) {
        tell("That folder has more than one session. Choose one mission's folder inside Sessions.");
        return;
    }
    await loadEntries(entries);
}

document.querySelector("input[name=outing]").addEventListener("change", async event => {
    try {
        await loadEntries(await filesToEntries(event.target.files));
    } catch (error) {
        tell(error.message);
    }
    event.target.value = "";
});

document.querySelector("input[name=maps]").addEventListener("change", async event => {
    try {
        for (const file of event.target.files) {
            mapView.addPack(await gismo.MapPack.open(file));
        }
    } catch (error) {
        tell(error.message);
    }
    event.target.value = "";
    updateLayers();
    if (!state.title) {
        fitOuting();
    }
});

const onlineToggle = document.querySelector("input[name=online]");

function applyOnline() {
    mapView.setOnline(onlineToggle.checked && Boolean(state.title));
    updateLayers();
}

onlineToggle.addEventListener("change", applyOnline);

layerSelect.addEventListener("change", () => {
    mapView.layer = Number(layerSelect.value);
    mapView.scheduleRender();
});

document.querySelector("header").addEventListener("click", event => {
    if (event.target.closest("button[data-action]")?.dataset.action === "open-folder") {
        openFolder().catch(error => tell(error.message));
    }
});

document.querySelector("figcaption").addEventListener("click", event => {
    const action = event.target.closest("button[data-action]")?.dataset.action;
    if (action === "zoom-in") {
        mapView.zoomCentre(0.5);
    } else if (action === "zoom-out") {
        mapView.zoomCentre(2);
    } else if (action === "fit") {
        fitOuting();
    }
});
