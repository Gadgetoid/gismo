"use strict";

const markerRadius = 11;
const viaRadius = 6;
const maxRouteSnapMetres = 150;
const historyLimit = 200;
const typingMergeMs = 1500;
const segmentTolerance = 6;
const hintWidth = 320;
const hintHeight = 240;

const state = {
    places: [],
    routes: [],
    legs: new Map(),
    placesAttribution: "",
    mission: newMission(),
    photos: new Map(),
    selected: -1,
    card: null,
    missionsFolder: null,
};

const canvas = document.querySelector("figure canvas");
const mapView = new MapView(canvas, {
    emptyMessage: "Open the GISmo card, or choose its .gmap map files",
    draw: () => {
        drawPlaces();
        drawWaypoints();
    },
    hit: (x, y) => {
        const index = waypointAt(x, y);
        return index >= 0 ? index : null;
    },
    press: index => select(index),
    drag: (index, grid) => {
        const position = gismo.gridToWgs84(grid.east, grid.north);
        const waypoint = state.mission.waypoints[index];
        waypoint.latitude = position.latitude;
        waypoint.longitude = position.longitude;
        updateWaypointItem(index);
    },
    dragEnd: () => missionChanged(),
    click: (x, y, item) => {
        if (item === null) {
            addWaypointAt(x, y);
        }
    },
    hover: (x, y, grid, pressed) => {
        const position = gismo.gridToWgs84(grid.east, grid.north);
        cursorOutput.value = `${gismo.gridReference(grid.east, grid.north, 8)}   ${position.latitude.toFixed(5)}, ${position.longitude.toFixed(5)}`;
        if (!pressed) {
            canvas.toggleAttribute("data-insert", waypointAt(x, y) < 0 && segmentAt(x, y) >= 0);
        }
    },
});
const context = mapView.context;
const form = document.forms.mission;
const waypointList = form.querySelector("ol");
const template = document.querySelector("template");
const placesSearch = document.querySelector("input[name=places]");
const placesList = document.querySelector("[aria-labelledby=places-heading] ul");
const problemsList = document.querySelector("[role=status]");
const preview = document.querySelector("output[name=preview]");
const cursorOutput = document.querySelector("output[name=cursor]");
const attribution = document.querySelector("figcaption small");
const layerSelect = document.querySelector("select[name=layer]");
const missionSelect = document.querySelector("select[name=missions]");
const dialog = document.querySelector("dialog");

function newMission() {
    return {title: "", intro: "", waypoints: []};
}

function newWaypoint(name, latitude, longitude) {
    return {name, latitude, longitude, radius: 25, task: "visit", route: false, clue: "", prompt: "", choices: [], answer: "", photo: ""};
}

function tell(message) {
    dialog.querySelector("p").textContent = message;
    dialog.showModal();
}

function metresPerPixel() {
    return mapView.metresPerPixel();
}

function toScreen(east, north) {
    return mapView.toScreen(east, north);
}

function toGrid(x, y) {
    return mapView.toGrid(x, y);
}

function waypointGrid(waypoint) {
    return gismo.wgs84ToGrid(waypoint.latitude, waypoint.longitude);
}

function scheduleRender() {
    mapView.scheduleRender();
}

function drawPlaces() {
    const scale = metresPerPixel();
    if (scale > 8 || !state.places.length) {
        return;
    }
    const {width, height} = mapView.size();
    context.font = "12px system-ui, sans-serif";
    context.textBaseline = "middle";
    const labelled = [];
    for (const place of state.places) {
        const point = toScreen(place.east, place.north);
        if (point.x < -50 || point.y < -10 || point.x > width + 50 || point.y > height + 10) {
            continue;
        }
        context.beginPath();
        context.arc(point.x, point.y, 3.5, 0, Math.PI * 2);
        context.fillStyle = place.name ? "#0b7a75" : "#7f8c8d";
        context.strokeStyle = "white";
        context.lineWidth = 1.5;
        context.fill();
        context.stroke();
        if (scale <= 1 && place.name) {
            const box = {left: point.x + 5, right: point.x + 8 + context.measureText(place.name).width, top: point.y - 7, bottom: point.y + 7};
            if (labelled.some(other => box.left < other.right && box.right > other.left && box.top < other.bottom && box.bottom > other.top)) {
                continue;
            }
            labelled.push(box);
            context.lineWidth = 3;
            context.strokeStyle = "rgb(255 255 255 / 0.85)";
            context.strokeText(place.name, point.x + 6, point.y);
            context.fillStyle = "#0b4f4c";
            context.fillText(place.name, point.x + 6, point.y);
        }
    }
}

function stopNumber(index) {
    const waypoints = state.mission.waypoints;
    if (waypoints[index].task === "via") {
        return 0;
    }
    return waypoints.slice(0, index + 1).filter(waypoint => waypoint.task !== "via").length;
}

function walkingLeg(index) {
    const waypoints = state.mission.waypoints;
    if (index < 1 || !waypoints[index].route || !state.routes.length) {
        return null;
    }
    const from = waypointGrid(waypoints[index - 1]);
    const to = waypointGrid(waypoints[index]);
    const key = `${from.east.toFixed(1)},${from.north.toFixed(1)},${to.east.toFixed(1)},${to.north.toFixed(1)}`;
    if (state.legs.has(key)) {
        return state.legs.get(key);
    }
    if (mapView.draggedItem() !== null) {
        return null;
    }
    let best = null;
    for (const graph of state.routes) {
        if (!graph.covers(from.east, from.north) || !graph.covers(to.east, to.north)) {
            continue;
        }
        const start = graph.nearest(from.east, from.north);
        const goal = graph.nearest(to.east, to.north);
        const snap = Math.max(graph.distanceTo(start, from), graph.distanceTo(goal, to));
        if (snap <= maxRouteSnapMetres && (!best || snap < best.snap)) {
            best = {graph, start, goal, snap};
        }
    }
    let leg = null;
    if (best) {
        const route = best.graph.find(best.start, best.goal);
        if (route) {
            const points = [from, ...route.points, to];
            let metres = 0;
            for (let point = 1; point < points.length; point++) {
                metres += Math.hypot(points[point].east - points[point - 1].east, points[point].north - points[point - 1].north);
            }
            leg = {points, metres};
        }
    }
    state.legs.set(key, leg);
    queueMicrotask(() => updateWaypointItem(index));
    return leg;
}

function drawWaypoints() {
    const scale = metresPerPixel();
    const points = state.mission.waypoints.map(waypoint => {
        const grid = waypointGrid(waypoint);
        return {...toScreen(grid.east, grid.north), radius: waypoint.radius / scale};
    });
    for (let index = 1; index < points.length; index++) {
        const leg = walkingLeg(index);
        context.beginPath();
        if (leg) {
            leg.points.forEach((grid, position) => {
                const point = toScreen(grid.east, grid.north);
                position ? context.lineTo(point.x, point.y) : context.moveTo(point.x, point.y);
            });
            context.strokeStyle = "rgb(0 110 255 / 0.85)";
            context.lineWidth = 4;
        } else {
            context.moveTo(points[index - 1].x, points[index - 1].y);
            context.lineTo(points[index].x, points[index].y);
            context.setLineDash([8, 6]);
            context.strokeStyle = "rgb(124 60 200 / 0.8)";
            context.lineWidth = 3;
        }
        context.stroke();
        context.setLineDash([]);
    }
    context.font = "bold 12px system-ui, sans-serif";
    context.textAlign = "center";
    context.textBaseline = "middle";
    points.forEach((point, index) => {
        const selected = index === state.selected;
        if (point.radius > markerRadius + 2) {
            context.beginPath();
            context.arc(point.x, point.y, point.radius, 0, Math.PI * 2);
            context.fillStyle = selected ? "rgb(255 122 0 / 0.15)" : "rgb(124 60 200 / 0.1)";
            context.strokeStyle = selected ? "#ff7a00" : "rgb(124 60 200 / 0.6)";
            context.lineWidth = 2;
            context.fill();
            context.stroke();
        }
        const stop = stopNumber(index);
        context.beginPath();
        context.arc(point.x, point.y, stop ? markerRadius : viaRadius, 0, Math.PI * 2);
        context.fillStyle = selected ? "#ff7a00" : "#7c3cc8";
        context.strokeStyle = "white";
        context.lineWidth = 2.5;
        context.fill();
        context.stroke();
        if (stop) {
            context.fillStyle = "white";
            context.fillText(String(stop), point.x, point.y + 0.5);
        }
    });
    context.textAlign = "start";
}

function waypointAt(x, y) {
    for (let index = state.mission.waypoints.length - 1; index >= 0; index--) {
        const grid = waypointGrid(state.mission.waypoints[index]);
        const point = toScreen(grid.east, grid.north);
        if (Math.hypot(point.x - x, point.y - y) <= markerRadius + 3) {
            return index;
        }
    }
    return -1;
}

function segmentAt(x, y) {
    const points = state.mission.waypoints.map(waypoint => {
        const grid = waypointGrid(waypoint);
        return toScreen(grid.east, grid.north);
    });
    let best = -1;
    let bestDistance = segmentTolerance;
    for (let index = 0; index + 1 < points.length; index++) {
        const start = points[index];
        const end = points[index + 1];
        const lengthSquared = (end.x - start.x) ** 2 + (end.y - start.y) ** 2;
        if (lengthSquared === 0) {
            continue;
        }
        const along = Math.max(0, Math.min(1, ((x - start.x) * (end.x - start.x) + (y - start.y) * (end.y - start.y)) / lengthSquared));
        const distance = Math.hypot(x - (start.x + along * (end.x - start.x)), y - (start.y + along * (end.y - start.y)));
        if (distance <= bestDistance) {
            best = index + 1;
            bestDistance = distance;
        }
    }
    return best;
}

function placeAt(x, y) {
    if (metresPerPixel() > 8) {
        return null;
    }
    let best = null;
    let bestDistance = 8;
    for (const place of state.places) {
        const point = toScreen(place.east, place.north);
        const distance = Math.hypot(point.x - x, point.y - y);
        if (distance < bestDistance) {
            best = place;
            bestDistance = distance;
        }
    }
    return best;
}

function placeLabel(place) {
    return place.name || place.kind.replace(/_/g, " ").replace(/^./, letter => letter.toUpperCase());
}

function addWaypoint(name, latitude, longitude, index = state.mission.waypoints.length) {
    state.mission.waypoints.splice(index, 0, newWaypoint(name, latitude, longitude));
    select(index);
    renderWaypoints();
    missionChanged();
}

function select(index) {
    state.selected = index;
    for (const [position, item] of [...waypointList.children].entries()) {
        item.setAttribute("aria-current", String(position === index));
        if (position === index) {
            item.querySelector("details").open = true;
            item.scrollIntoView({block: "nearest", behavior: "smooth"});
        }
    }
    scheduleRender();
}

function addWaypointAt(x, y) {
    const segment = segmentAt(x, y);
    const index = segment >= 0 ? segment : state.mission.waypoints.length;
    const place = placeAt(x, y);
    if (place) {
        addWaypoint(placeLabel(place), place.latitude, place.longitude, index);
        return;
    }
    const grid = toGrid(x, y);
    const position = gismo.gridToWgs84(grid.east, grid.north);
    addWaypoint(`Waypoint ${state.mission.waypoints.length + 1}`, position.latitude, position.longitude, index);
}

const taskNames = {visit: "visit", photo: "photo", choice: "multiple choice", count: "count", via: "route point"};

function updateWaypointItem(index) {
    const item = waypointList.children[index];
    const waypoint = state.mission.waypoints[index];
    if (!item) {
        return;
    }
    item.dataset.task = waypoint.task;
    const small = document.createElement("small");
    const details = [waypoint.task === "via" ? "" : taskNames[waypoint.task], waypoint.route ? "walking route" : "", waypoint.photo && waypoint.task !== "via" ? "photo hint" : ""].filter(Boolean);
    small.textContent = details.join(", ");
    const stop = stopNumber(index);
    item.querySelector("summary").replaceChildren(stop ? `${stop}. ${waypoint.name || "Unnamed"} ` : `${waypoint.name || "Route point"} `, small);
    const grid = waypointGrid(waypoint);
    const leg = walkingLeg(index);
    const walking = leg ? `, walking route ${leg.metres < 1000 ? `${Math.round(leg.metres)} m` : `${(leg.metres / 1000).toFixed(1)} km`}` : "";
    item.querySelector("output[name=where]").value = `${gismo.gridReference(grid.east, grid.north, 8)} (${waypoint.latitude.toFixed(6)}, ${waypoint.longitude.toFixed(6)})${walking}`;
    const answer = item.querySelector("select[name=answer]");
    const options = ["", ...waypoint.choices];
    if ([...answer.options].map(option => option.value).join("|") !== options.join("|")) {
        answer.replaceChildren(...options.map(choice => new Option(choice || "(any answer)", choice)));
    }
    answer.value = waypoint.answer;
    const image = item.querySelector("img");
    const photo = waypoint.photo ? state.photos.get(waypoint.photo) : null;
    if (photo) {
        image.src = photo.url;
        image.alt = `Photo hint for ${waypoint.name}`;
    } else {
        image.removeAttribute("src");
        image.alt = "";
    }
}

function renderWaypoints() {
    waypointList.replaceChildren(...state.mission.waypoints.map((waypoint, index) => {
        const item = template.content.firstElementChild.cloneNode(true);
        item.dataset.index = String(index);
        item.querySelector("summary").draggable = true;
        for (const field of ["name", "task", "radius", "clue", "prompt"]) {
            item.querySelector(`[name=${field}]`).value = waypoint[field];
        }
        item.querySelector("[name=choices]").value = waypoint.choices.join("\n");
        item.querySelector("[name=route]").checked = waypoint.route;
        item.querySelector("details").open = index === state.selected;
        item.setAttribute("aria-current", String(index === state.selected));
        return item;
    }));
    state.mission.waypoints.forEach((_, index) => updateWaypointItem(index));
}

function moveWaypoint(from, to) {
    const waypoints = state.mission.waypoints;
    if (from === to || from < 0 || from >= waypoints.length) {
        return;
    }
    const selected = waypoints[state.selected];
    const [moved] = waypoints.splice(from, 1);
    waypoints.splice(to, 0, moved);
    state.selected = waypoints.indexOf(selected);
    renderWaypoints();
    missionChanged();
}

function clearDropMarks() {
    for (const item of waypointList.querySelectorAll("li[data-drop]")) {
        delete item.dataset.drop;
    }
}

waypointList.addEventListener("dragstart", event => {
    const item = event.target.closest?.("li[data-index]");
    if (!item || event.target.tagName !== "SUMMARY") {
        return;
    }
    state.dragging = Number(item.dataset.index);
    event.dataTransfer.effectAllowed = "move";
    event.dataTransfer.setData("text/plain", item.dataset.index);
});

waypointList.addEventListener("dragover", event => {
    const item = event.target.closest("li[data-index]");
    if (!item || state.dragging === undefined) {
        return;
    }
    event.preventDefault();
    const box = item.getBoundingClientRect();
    const position = event.clientY < box.top + box.height / 2 ? "before" : "after";
    if (item.dataset.drop !== position) {
        clearDropMarks();
        item.dataset.drop = position;
    }
});

waypointList.addEventListener("drop", event => {
    const item = event.target.closest("li[data-index]");
    if (!item || state.dragging === undefined) {
        return;
    }
    event.preventDefault();
    const over = Number(item.dataset.index);
    const after = item.dataset.drop === "after";
    const from = state.dragging;
    let to = over + (after ? 1 : 0);
    if (to > from) {
        to--;
    }
    clearDropMarks();
    state.dragging = undefined;
    moveWaypoint(from, to);
});

waypointList.addEventListener("dragend", () => {
    clearDropMarks();
    state.dragging = undefined;
});

const history = {past: [], future: [], current: "", field: null, time: 0};

function resetHistory() {
    history.past = [];
    history.future = [];
    history.current = JSON.stringify(state.mission);
    history.field = null;
}

function recordHistory(field) {
    const now = JSON.stringify(state.mission);
    if (now === history.current) {
        return;
    }
    const merging = field && field === history.field && performance.now() - history.time < typingMergeMs;
    if (!merging) {
        history.past.push(history.current);
        if (history.past.length > historyLimit) {
            history.past.shift();
        }
    }
    history.future = [];
    history.current = now;
    history.field = field || null;
    history.time = performance.now();
}

function restoreHistory(text) {
    state.mission = JSON.parse(text);
    history.current = text;
    history.field = null;
    form.elements.title.value = state.mission.title;
    form.elements.intro.value = state.mission.intro;
    state.selected = Math.min(state.selected, state.mission.waypoints.length - 1);
    if (state.selected < 0 && state.mission.waypoints.length) {
        state.selected = 0;
    }
    renderWaypoints();
    missionChanged();
}

function undo() {
    if (history.past.length) {
        history.future.push(history.current);
        restoreHistory(history.past.pop());
    }
}

function redo() {
    if (history.future.length) {
        history.past.push(history.current);
        restoreHistory(history.future.pop());
    }
}

function deleteWaypoint(index) {
    const waypoints = state.mission.waypoints;
    if (index < 0 || index >= waypoints.length) {
        return;
    }
    waypoints.splice(index, 1);
    state.selected = Math.min(index, waypoints.length - 1);
    renderWaypoints();
    missionChanged();
}

function missionChanged(field) {
    recordHistory(field);
    const problems = gismo.missionProblems(state.mission);
    problemsList.replaceChildren(...problems.map(problem => Object.assign(document.createElement("li"), {textContent: problem})));
    preview.value = state.mission.waypoints.length ? gismo.writeMission(state.mission) : "";
    scheduleRender();
}

form.addEventListener("input", event => {
    const field = event.target;
    if (field.name === "title" || field.name === "intro") {
        state.mission[field.name] = field.value;
        if (field.name === "title" && !form.elements.file.dataset.edited) {
            form.elements.file.value = gismo.slug(field.value);
        }
        missionChanged(field);
        return;
    }
    if (field.name === "file") {
        field.dataset.edited = "true";
        return;
    }
    const item = field.closest("li[data-index]");
    if (!item || field.type === "file") {
        return;
    }
    const index = Number(item.dataset.index);
    const waypoint = state.mission.waypoints[index];
    switch (field.name) {
    case "radius":
        waypoint.radius = Math.max(1, Math.round(Number(field.value) || 25));
        break;
    case "route":
        waypoint.route = field.checked;
        break;
    case "choices":
        waypoint.choices = field.value.split("\n").map(choice => choice.trim()).filter(Boolean);
        if (!waypoint.choices.includes(waypoint.answer)) {
            waypoint.answer = "";
        }
        break;
    default:
        waypoint[field.name] = field.value;
    }
    if (field.name === "task") {
        state.mission.waypoints.forEach((_, position) => updateWaypointItem(position));
    } else {
        updateWaypointItem(index);
    }
    missionChanged(field);
});

form.addEventListener("change", async event => {
    const field = event.target;
    if (field.name !== "photo" || !field.files.length) {
        return;
    }
    const index = Number(field.closest("li[data-index]").dataset.index);
    try {
        await attachPhoto(index, field.files[0]);
    } catch (error) {
        tell(`That photo couldn't be read: ${error.message}`);
    }
    field.value = "";
});

form.addEventListener("click", event => {
    const button = event.target.closest("button[data-action]");
    const item = button?.closest("li[data-index]");
    if (!item) {
        return;
    }
    const index = Number(item.dataset.index);
    const waypoints = state.mission.waypoints;
    switch (button.dataset.action) {
    case "up":
    case "down": {
        const target = button.dataset.action === "up" ? index - 1 : index + 1;
        if (target < 0 || target >= waypoints.length) {
            return;
        }
        [waypoints[index], waypoints[target]] = [waypoints[target], waypoints[index]];
        state.selected = target;
        renderWaypoints();
        break;
    }
    case "show": {
        const grid = waypointGrid(waypoints[index]);
        mapView.centreOn(grid.east, grid.north, Math.min(mapView.metresPerPixel(), 2));
        select(index);
        break;
    }
    case "delete":
        deleteWaypoint(index);
        return;
    case "remove-photo":
        waypoints[index].photo = "";
        updateWaypointItem(index);
        break;
    }
    missionChanged();
});

waypointList.addEventListener("toggle", event => {
    const item = event.target.closest("li[data-index]");
    if (item && event.target.open) {
        state.selected = Number(item.dataset.index);
        for (const other of waypointList.children) {
            other.setAttribute("aria-current", String(other === item));
        }
        scheduleRender();
    }
}, true);

async function attachPhoto(index, file) {
    const waypoint = state.mission.waypoints[index];
    const bitmap = await createImageBitmap(file, {imageOrientation: "from-image"});
    const scratch = document.createElement("canvas");
    scratch.width = hintWidth;
    scratch.height = hintHeight;
    const draw = scratch.getContext("2d");
    const scale = Math.max(hintWidth / bitmap.width, hintHeight / bitmap.height);
    const width = bitmap.width * scale;
    const height = bitmap.height * scale;
    draw.drawImage(bitmap, (hintWidth - width) / 2, (hintHeight - height) / 2, width, height);
    const pixels = draw.getImageData(0, 0, hintWidth, hintHeight).data;
    let name = `${gismo.slug(waypoint.name || "hint")}.bmp`;
    for (let suffix = 2; [...state.mission.waypoints].some((other, position) => position !== index && other.photo === name); suffix++) {
        name = `${gismo.slug(waypoint.name || "hint")}-${suffix}.bmp`;
    }
    state.photos.set(name, {bytes: gismo.bmp565(pixels, hintWidth, hintHeight), url: scratch.toDataURL("image/png"), changed: true});
    waypoint.photo = name;
    updateWaypointItem(index);
    missionChanged();
}

function renderPlaces() {
    const query = placesSearch.value.trim().toLowerCase();
    const centre = mapView.view;
    const matches = state.places
        .filter(place => place.name && (!query || place.name.toLowerCase().includes(query) || place.kind.includes(query)))
        .map(place => ({place, distance: Math.hypot(place.east - centre.east, place.north - centre.north)}))
        .sort((a, b) => a.distance - b.distance)
        .slice(0, 60);
    placesList.replaceChildren(...matches.map(({place, distance}) => {
        const item = document.createElement("li");
        const button = document.createElement("button");
        button.type = "button";
        button.append(`${place.name} `);
        const small = document.createElement("small");
        small.textContent = `${place.kind.replace(/_/g, " ")}, ${distance < 1000 ? `${Math.round(distance)} m` : `${(distance / 1000).toFixed(1)} km`}`;
        button.append(small);
        button.addEventListener("click", () => {
            addWaypoint(place.name, place.latitude, place.longitude);
            mapView.centreOn(place.east, place.north);
        });
        item.append(button);
        return item;
    }));
}

placesSearch.addEventListener("input", renderPlaces);

function updateLayers() {
    layerSelect.replaceChildren(...mapView.details.map((pack, index) => new Option(pack.name, String(index))));
    layerSelect.value = String(mapView.layer);
    layerSelect.disabled = mapView.details.length < 2;
    const notes = mapView.attributions();
    if (state.placesAttribution) {
        notes.push(state.placesAttribution);
    }
    const sentences = notes.flatMap(note => note.split(/\.\s+/)).map(sentence => sentence.replace(/\.$/, "").trim()).filter(Boolean);
    attribution.textContent = `${[...new Set(sentences)].join(". ")}.`;
}

layerSelect.addEventListener("change", () => {
    mapView.layer = Number(layerSelect.value);
    scheduleRender();
});

async function loadMapFile(name, file) {
    if (name.endsWith(".gmap")) {
        mapView.addPack(await gismo.MapPack.open(file));
    } else if (name.endsWith(".groute")) {
        state.routes.push(new gismo.RouteGraph(await file.arrayBuffer(), name));
        state.legs.clear();
    } else if (name.endsWith(".json")) {
        const data = JSON.parse(await file.text());
        state.places = data.places.map(place => ({...place, ...gismo.wgs84ToGrid(place.latitude, place.longitude)}));
        state.placesAttribution = data.attribution || "";
    }
}

function fitView() {
    const points = state.mission.waypoints.map(waypointGrid);
    mapView.fit(points.length ? points : mapView.mapBounds());
}

function showMission(mission, fileName) {
    state.mission = mission;
    state.selected = mission.waypoints.length ? 0 : -1;
    form.elements.title.value = mission.title;
    form.elements.intro.value = mission.intro;
    form.elements.file.value = fileName || gismo.slug(mission.title);
    form.elements.file.dataset.edited = fileName ? "true" : "";
    renderWaypoints();
    missionChanged();
    resetHistory();
    fitView();
    renderPlaces();
}

async function openMissionText(text, fileName, folder) {
    let mission;
    try {
        mission = gismo.parseMission(text);
    } catch (error) {
        tell(`${fileName}: ${error.message}`);
        return;
    }
    state.photos = new Map();
    for (const waypoint of mission.waypoints) {
        if (!waypoint.photo || !folder) {
            continue;
        }
        try {
            const handle = await folder.getFileHandle(waypoint.photo);
            const file = await handle.getFile();
            const bytes = new Uint8Array(await file.arrayBuffer());
            state.photos.set(waypoint.photo, {bytes, url: URL.createObjectURL(file), changed: false});
        } catch {
            state.photos.delete(waypoint.photo);
        }
    }
    showMission(mission, fileName.replace(/\.txt$/i, ""));
}

async function findFolder(root, path) {
    let folder = root;
    for (const part of path) {
        folder = await folder.getDirectoryHandle(part).catch(() => null);
        if (!folder) {
            return null;
        }
    }
    return folder;
}

async function openCard() {
    if (!window.showDirectoryPicker) {
        tell("Opening the card folder needs Chrome or Edge. Use “Maps and places…” and “Mission file…” instead.");
        return;
    }
    let root;
    try {
        root = await window.showDirectoryPicker({id: "gismo-card", mode: "readwrite"});
    } catch {
        return;
    }
    const base = (await findFolder(root, ["Maps"])) ? root : await findFolder(root, ["GISmo"]) || await findFolder(root, ["GIZmo"]);
    const maps = base && await findFolder(base, ["Maps"]);
    if (!maps) {
        tell("That folder doesn't have a Maps folder. Choose the GISmo folder on the card, or the card itself.");
        return;
    }
    mapView.clearPacks();
    state.routes = [];
    state.legs.clear();
    for await (const [name, handle] of maps.entries()) {
        if (handle.kind === "file" && !name.startsWith("._") && /\.(gmap|groute|json)$/.test(name)) {
            await loadMapFile(name, await handle.getFile());
        }
    }
    state.card = base;
    document.querySelector("button[data-action=backup-card]").disabled = false;
    state.missionsFolder = await base.getDirectoryHandle("Missions", {create: true});
    const missions = [];
    for await (const [name, handle] of state.missionsFolder.entries()) {
        if (handle.kind === "file" && name.endsWith(".txt") && !name.startsWith("._")) {
            missions.push(name);
        }
    }
    missions.sort();
    missionSelect.replaceChildren(new Option("New mission", ""), ...missions.map(name => new Option(name.replace(/\.txt$/, ""), name)));
    missionSelect.disabled = false;
    updateLayers();
    if (missions.length) {
        missionSelect.value = missions[0];
        await openSelectedMission();
    } else {
        showMission(newMission(), "");
    }
}

async function openSelectedMission() {
    const name = missionSelect.value;
    if (!name) {
        state.photos = new Map();
        showMission(newMission(), "");
        return;
    }
    const file = await (await state.missionsFolder.getFileHandle(name)).getFile();
    await openMissionText(await file.text(), name, state.missionsFolder);
}

missionSelect.addEventListener("change", openSelectedMission);

async function saveAs(name, bytes, type, description) {
    if (window.showSaveFilePicker) {
        let handle;
        try {
            handle = await window.showSaveFilePicker({suggestedName: name, types: [{description, accept: {[type]: [name.slice(name.lastIndexOf("."))]}}]});
        } catch {
            return false;
        }
        const writable = await handle.createWritable();
        await writable.write(bytes);
        await writable.close();
        return true;
    }
    download(name, bytes, type);
    return true;
}

function missionStem() {
    return gismo.slug(form.elements.file.value || state.mission.title || "mission");
}

async function exportMission() {
    const stem = missionStem();
    const files = [{name: `Missions/${stem}.txt`, bytes: gismo.writeMission(state.mission)}];
    for (const name of new Set(state.mission.waypoints.map(waypoint => waypoint.photo).filter(Boolean))) {
        const photo = state.photos.get(name);
        if (photo) {
            files.push({name: `Missions/${name}`, bytes: photo.bytes});
        }
    }
    if (await saveAs(`${stem}.zip`, gismo.zip(files), "application/zip", "Zip archive")) {
        const hints = files.length - 1;
        tell(`Exported ${stem}.zip: the mission${hints ? ` and ${hints} photo hint${hints > 1 ? "s" : ""}` : ""}. Unzip it into the GISmo folder on a card to use it.`);
    }
}

async function collectFiles(folder, prefix, files) {
    for await (const [name, handle] of folder.entries()) {
        if (name.startsWith("._") || name === ".DS_Store") {
            continue;
        }
        if (handle.kind === "directory") {
            await collectFiles(handle, `${prefix}${name}/`, files);
        } else {
            files.push({name: `${prefix}${name}`, bytes: new Uint8Array(await (await handle.getFile()).arrayBuffer())});
        }
    }
}

async function backUpCard() {
    if (!state.card) {
        return;
    }
    const files = [];
    for (const name of ["Missions", "Sessions"]) {
        const folder = await findFolder(state.card, [name]);
        if (folder) {
            await collectFiles(folder, `${name}/`, files);
        }
    }
    const today = new Date();
    const stamp = `${today.getFullYear()}-${String(today.getMonth() + 1).padStart(2, "0")}-${String(today.getDate()).padStart(2, "0")}`;
    const archive = gismo.zip(files, today);
    if (await saveAs(`gismo-backup-${stamp}.zip`, archive, "application/zip", "Zip archive")) {
        tell(`Backed up ${files.length} files (${(archive.length / 1e6).toFixed(1)} MB) from Missions and Sessions.`);
    }
}

function download(name, bytes, type) {
    const link = document.createElement("a");
    link.href = URL.createObjectURL(new Blob([bytes], {type}));
    link.download = name;
    link.click();
    setTimeout(() => URL.revokeObjectURL(link.href), 1000);
}

async function saveMission() {
    const problems = gismo.missionProblems(state.mission);
    if (!state.mission.title.trim()) {
        problems.unshift("Give the mission a title.");
    }
    if (problems.length) {
        tell(`Not saved yet: ${problems.join(" ")}`);
        return;
    }
    const stem = gismo.slug(form.elements.file.value || state.mission.title);
    form.elements.file.value = stem;
    const text = gismo.writeMission(state.mission);
    const photos = state.mission.waypoints.map(waypoint => waypoint.photo).filter(Boolean);
    if (state.missionsFolder) {
        const writeFile = async (name, data) => {
            const writable = await (await state.missionsFolder.getFileHandle(name, {create: true})).createWritable();
            await writable.write(data);
            await writable.close();
        };
        await writeFile(`${stem}.txt`, text);
        for (const name of photos) {
            const photo = state.photos.get(name);
            if (photo?.changed) {
                await writeFile(name, photo.bytes);
                photo.changed = false;
            }
        }
        if (![...missionSelect.options].some(option => option.value === `${stem}.txt`)) {
            missionSelect.append(new Option(stem, `${stem}.txt`));
        }
        missionSelect.value = `${stem}.txt`;
        tell(`Saved ${stem}.txt${photos.length ? ` and ${photos.length} photo hint${photos.length > 1 ? "s" : ""}` : ""} to the card's Missions folder.`);
        return;
    }
    download(`${stem}.txt`, text, "text/plain");
    for (const name of photos) {
        const photo = state.photos.get(name);
        if (photo?.changed) {
            download(name, photo.bytes, "image/bmp");
        }
    }
}

document.querySelector("header").addEventListener("click", event => {
    const action = event.target.closest("button[data-action]")?.dataset.action;
    if (action === "open-card") {
        openCard().catch(error => tell(error.message));
    } else if (action === "new-mission") {
        state.photos = new Map();
        missionSelect.value = "";
        showMission(newMission(), "");
    } else if (action === "save-mission") {
        saveMission().catch(error => tell(`Saving failed: ${error.message}`));
    } else if (action === "export-mission") {
        exportMission().catch(error => tell(`Export failed: ${error.message}`));
    } else if (action === "backup-card") {
        backUpCard().catch(error => tell(`Backup failed: ${error.message}`));
    }
});

document.querySelector("figcaption").addEventListener("click", event => {
    const action = event.target.closest("button[data-action]")?.dataset.action;
    if (action === "zoom-in") {
        mapView.zoomCentre(0.5);
    } else if (action === "zoom-out") {
        mapView.zoomCentre(2);
    } else if (action === "fit") {
        fitView();
    }
});

document.querySelector("input[name=maps]").addEventListener("change", async event => {
    try {
        for (const file of event.target.files) {
            await loadMapFile(file.name, file);
        }
    } catch (error) {
        tell(error.message);
    }
    event.target.value = "";
    updateLayers();
    fitView();
    renderPlaces();
});

document.querySelector("input[name=mission]").addEventListener("change", async event => {
    const file = event.target.files[0];
    if (file) {
        state.missionsFolder = null;
        await openMissionText(await file.text(), file.name, null);
    }
    event.target.value = "";
});

showMission(newMission(), "");

function isTyping(target) {
    if (target.closest?.("textarea, select, [contenteditable]")) {
        return true;
    }
    return target.tagName === "INPUT" && !["checkbox", "radio", "button", "file"].includes(target.type);
}

document.addEventListener("keydown", event => {
    const command = event.ctrlKey || event.metaKey;
    const key = event.key.toLowerCase();
    if (command && key === "s") {
        event.preventDefault();
        saveMission().catch(error => tell(`Saving failed: ${error.message}`));
        return;
    }
    if (dialog.open || isTyping(event.target)) {
        return;
    }
    if (command && key === "z") {
        event.preventDefault();
        event.shiftKey ? redo() : undo();
    } else if (command && key === "y") {
        event.preventDefault();
        redo();
    } else if ((event.key === "Delete" || event.key === "Backspace") && state.selected >= 0) {
        event.preventDefault();
        deleteWaypoint(state.selected);
    } else if (event.key === "Escape" && state.selected >= 0) {
        select(-1);
    }
});

gismo.fetchDefaultMaps(name => /\.(gmap|groute|json)$/.test(name)).then(async files => {
    if (!files.length || mapView.hasMaps()) {
        return;
    }
    for (const file of files) {
        await loadMapFile(file.name, file);
    }
    updateLayers();
    fitView();
    renderPlaces();
});
