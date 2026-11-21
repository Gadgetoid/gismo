"use strict";

class MapView {
    static finestMetresPerPixel = 0.25;
    static coarsestMetresPerPixel = 1024;
    static overviewThreshold = 10;
    static dragThreshold = 5;
    static wheelPixelsPerDoubling = 300;
    static pinchPixelsPerDoubling = 100;
    static onlineTileUrl = "https://tile.openstreetmap.org/{z}/{x}/{y}.png";
    static onlineAttribution = "Background map (c) OpenStreetMap contributors";
    static onlineMaxZoom = 19;
    static onlineTileLimit = 120;
    static onlineParentLevels = 5;
    static onlineCacheLimit = 400;
    static equatorMetresPerPixel = 156543.03392;

    constructor(canvas, hooks) {
        this.canvas = canvas;
        this.context = canvas.getContext("2d");
        this.hooks = hooks;
        this.view = {east: 537876, north: 263579, metresPerPixel: 2};
        this.overview = null;
        this.details = [];
        this.layer = 0;
        this.tiles = new Map();
        this.online = false;
        this.onlineTiles = new Map();
        this.pointer = null;
        this.renderQueued = false;
        canvas.addEventListener("pointerdown", event => this.pointerDown(event));
        canvas.addEventListener("pointermove", event => this.pointerMove(event));
        canvas.addEventListener("pointerup", event => this.pointerUp(event));
        canvas.addEventListener("wheel", event => this.wheel(event), {passive: false});
        new ResizeObserver(() => this.scheduleRender()).observe(canvas);
    }

    metresPerPixel() {
        return this.view.metresPerPixel;
    }

    size() {
        return {width: this.canvas.clientWidth, height: this.canvas.clientHeight};
    }

    toScreen(east, north) {
        const {width, height} = this.size();
        const scale = this.view.metresPerPixel;
        return {x: (east - this.view.east) / scale + width / 2, y: (this.view.north - north) / scale + height / 2};
    }

    toGrid(x, y) {
        const {width, height} = this.size();
        const scale = this.view.metresPerPixel;
        return {east: this.view.east + (x - width / 2) * scale, north: this.view.north - (y - height / 2) * scale};
    }

    hasMaps() {
        return Boolean(this.overview || this.details.length);
    }

    addPack(pack) {
        if (pack.finest >= MapView.overviewThreshold) {
            this.overview = pack;
        } else {
            this.details.push(pack);
            this.details.sort((a, b) => a.name.length - b.name.length);
        }
        this.scheduleRender();
    }

    clearPacks() {
        this.overview = null;
        this.details = [];
        this.layer = 0;
        this.tiles.clear();
    }

    attributions() {
        const notes = [...this.details, this.overview].filter(Boolean).map(pack => pack.properties.attribution).filter(Boolean);
        return this.online ? [...notes, MapView.onlineAttribution] : notes;
    }

    setOnline(enabled) {
        this.online = enabled;
        this.scheduleRender();
    }

    cachedOnlineTile(zoom, x, y) {
        const key = `${zoom}/${x}/${y}`;
        const tile = this.onlineTiles.get(key);
        if (!tile) {
            return null;
        }
        this.onlineTiles.delete(key);
        this.onlineTiles.set(key, tile);
        return tile.ready ? tile.image : null;
    }

    onlineTile(zoom, x, y) {
        const key = `${zoom}/${x}/${y}`;
        if (!this.onlineTiles.has(key)) {
            const tile = {image: new Image(), ready: false};
            tile.image.onload = () => {
                tile.ready = true;
                this.scheduleRender();
            };
            tile.image.src = MapView.onlineTileUrl.replace("{z}", zoom).replace("{x}", x).replace("{y}", y);
            this.onlineTiles.set(key, tile);
            while (this.onlineTiles.size > MapView.onlineCacheLimit) {
                this.onlineTiles.delete(this.onlineTiles.keys().next().value);
            }
        }
        return this.cachedOnlineTile(zoom, x, y);
    }

    drawOnline(ratio) {
        const {width, height} = this.size();
        const corners = [this.toGrid(0, 0), this.toGrid(width, 0), this.toGrid(0, height), this.toGrid(width, height)].map(point => gismo.gridToWgs84(point.east, point.north));
        const latitudes = corners.map(corner => corner.latitude);
        const longitudes = corners.map(corner => corner.longitude);
        const middle = (Math.min(...latitudes) + Math.max(...latitudes)) / 2 * Math.PI / 180;
        const target = this.view.metresPerPixel / ratio;
        const tileX = (longitude, scale) => Math.floor((longitude + 180) / 360 * scale);
        const tileY = (latitude, scale) => Math.floor((1 - Math.asinh(Math.tan(latitude * Math.PI / 180)) / Math.PI) / 2 * scale);
        const range = zoom => {
            const scale = 2 ** zoom;
            return {
                left: tileX(Math.min(...longitudes), scale),
                right: tileX(Math.max(...longitudes), scale),
                top: tileY(Math.max(...latitudes), scale),
                bottom: tileY(Math.min(...latitudes), scale),
            };
        };
        let zoom = Math.max(0, Math.min(MapView.onlineMaxZoom, Math.round(Math.log2(MapView.equatorMetresPerPixel * Math.cos(middle) / target))));
        let tiles = range(zoom);
        while (zoom > 0 && (tiles.right - tiles.left + 1) * (tiles.bottom - tiles.top + 1) > MapView.onlineTileLimit) {
            zoom--;
            tiles = range(zoom);
        }
        this.context.imageSmoothingEnabled = true;
        const scale = 2 ** zoom;
        for (let y = Math.max(0, tiles.top); y <= Math.min(scale - 1, tiles.bottom); y++) {
            for (let x = Math.max(0, tiles.left); x <= Math.min(scale - 1, tiles.right); x++) {
                const image = this.onlineTile(zoom, x, y);
                if (image) {
                    this.drawOnlineImage(ratio, image, 0, 0, 1, zoom, x, y);
                } else {
                    this.drawOnlineStandIn(ratio, zoom, x, y);
                }
            }
        }
        this.context.setTransform(ratio, 0, 0, ratio, 0, 0);
    }

    drawOnlineStandIn(ratio, zoom, x, y) {
        for (let levels = 1; levels <= MapView.onlineParentLevels && levels <= zoom; levels++) {
            const factor = 2 ** levels;
            const parent = this.cachedOnlineTile(zoom - levels, Math.floor(x / factor), Math.floor(y / factor));
            if (parent) {
                this.drawOnlineImage(ratio, parent, (x % factor) / factor, (y % factor) / factor, 1 / factor, zoom, x, y);
                return;
            }
        }
        if (zoom >= MapView.onlineMaxZoom) {
            return;
        }
        for (let child = 0; child < 4; child++) {
            const childX = x * 2 + child % 2;
            const childY = y * 2 + Math.floor(child / 2);
            const image = this.cachedOnlineTile(zoom + 1, childX, childY);
            if (image) {
                this.drawOnlineImage(ratio, image, 0, 0, 1, zoom + 1, childX, childY);
            }
        }
    }

    drawOnlineImage(ratio, image, sourceX, sourceY, sourceSize, zoom, x, y) {
        const scale = 2 ** zoom;
        const screen = (column, row) => {
            const longitude = column / scale * 360 - 180;
            const latitude = Math.atan(Math.sinh(Math.PI * (1 - 2 * row / scale))) * 180 / Math.PI;
            const grid = gismo.wgs84ToGrid(latitude, longitude);
            return this.toScreen(grid.east, grid.north);
        };
        const origin = screen(x, y);
        const across = screen(x + 1, y);
        const down = screen(x, y + 1);
        const size = image.naturalWidth || 256;
        this.context.setTransform(ratio * (across.x - origin.x) / size, ratio * (across.y - origin.y) / size, ratio * (down.x - origin.x) / size, ratio * (down.y - origin.y) / size, ratio * origin.x, ratio * origin.y);
        this.context.drawImage(image, sourceX * size, sourceY * size, sourceSize * size, sourceSize * size, 0, 0, size + 1, size + 1);
    }

    mapBounds() {
        const pack = this.details[0] || this.overview;
        if (!pack) {
            return [];
        }
        const bounds = pack.bounds();
        return [{east: bounds.west, north: bounds.north}, {east: bounds.east, north: bounds.south}];
    }

    scheduleRender() {
        if (!this.renderQueued) {
            this.renderQueued = true;
            requestAnimationFrame(() => {
                this.renderQueued = false;
                this.render();
            });
        }
    }

    tileBitmap(pack, level, column, row) {
        const key = `${pack.fileName}/${level}/${column}/${row}`;
        const cached = this.tiles.get(key);
        if (cached !== undefined) {
            return cached instanceof Promise ? null : cached;
        }
        const loading = pack.decode(level, column, row).then(async tile => {
            const bitmap = tile ? await createImageBitmap(new ImageData(tile.rgba, tile.width, tile.height)) : null;
            this.tiles.set(key, bitmap);
            this.scheduleRender();
        }).catch(() => this.tiles.set(key, null));
        this.tiles.set(key, loading);
        return null;
    }

    drawPack(pack) {
        const scale = this.view.metresPerPixel;
        const levelIndex = pack.bestLevel(scale);
        const level = pack.levels[levelIndex];
        const {width, height} = this.size();
        const topLeft = this.toGrid(0, 0);
        const bottomRight = this.toGrid(width, height);
        const size = level.span / scale;
        this.context.imageSmoothingEnabled = size < gismo.tileSize;
        for (let row = Math.floor(bottomRight.north / level.span); row <= Math.floor(topLeft.north / level.span); row++) {
            for (let column = Math.floor(topLeft.east / level.span); column <= Math.floor(bottomRight.east / level.span); column++) {
                const bitmap = this.tileBitmap(pack, levelIndex, column, row);
                if (bitmap) {
                    const corner = this.toScreen(column * level.span, (row + 1) * level.span);
                    this.context.drawImage(bitmap, Math.floor(corner.x), Math.floor(corner.y), Math.ceil(size) + 1, Math.ceil(size) + 1);
                }
            }
        }
    }

    render() {
        const ratio = window.devicePixelRatio || 1;
        const {width, height} = this.size();
        if (this.canvas.width !== Math.round(width * ratio) || this.canvas.height !== Math.round(height * ratio)) {
            this.canvas.width = Math.round(width * ratio);
            this.canvas.height = Math.round(height * ratio);
        }
        const context = this.context;
        context.setTransform(ratio, 0, 0, ratio, 0, 0);
        context.fillStyle = "#ece8dc";
        context.fillRect(0, 0, width, height);
        if (this.online) {
            this.drawOnline(ratio);
        }
        if (this.overview) {
            this.drawPack(this.overview);
        }
        const detail = this.details[this.layer];
        if (detail && (!this.overview || this.view.metresPerPixel <= detail.coarsest * 2)) {
            this.drawPack(detail);
        }
        this.hooks.draw?.(context);
        if (!this.hasMaps() && !this.online && this.hooks.emptyMessage) {
            context.fillStyle = "#6b665c";
            context.font = "16px system-ui, sans-serif";
            context.textAlign = "center";
            context.fillText(this.hooks.emptyMessage, width / 2, height / 2);
            context.textAlign = "start";
        }
    }

    zoomAround(x, y, factor) {
        const next = Math.min(MapView.coarsestMetresPerPixel, Math.max(MapView.finestMetresPerPixel, this.view.metresPerPixel * factor));
        if (next === this.view.metresPerPixel) {
            return;
        }
        const before = this.toGrid(x, y);
        this.view.metresPerPixel = next;
        const after = this.toGrid(x, y);
        this.view.east += before.east - after.east;
        this.view.north += before.north - after.north;
        this.scheduleRender();
    }

    zoomCentre(factor) {
        const {width, height} = this.size();
        this.zoomAround(width / 2, height / 2, factor);
    }

    centreOn(east, north, metresPerPixel = this.view.metresPerPixel) {
        this.view.east = east;
        this.view.north = north;
        this.view.metresPerPixel = metresPerPixel;
        this.scheduleRender();
    }

    fit(points, singlePointMetresPerPixel = 1) {
        if (!points.length) {
            return;
        }
        const west = Math.min(...points.map(point => point.east));
        const east = Math.max(...points.map(point => point.east));
        const south = Math.min(...points.map(point => point.north));
        const north = Math.max(...points.map(point => point.north));
        const {width, height} = this.size();
        const fitted = Math.max((east - west) / Math.max(width - 80, 1), (north - south) / Math.max(height - 80, 1));
        const smallest = points.length === 1 ? singlePointMetresPerPixel : MapView.finestMetresPerPixel;
        this.centreOn((west + east) / 2, (south + north) / 2, Math.min(MapView.coarsestMetresPerPixel, Math.max(smallest, fitted)));
    }

    dragging() {
        return Boolean(this.pointer?.moved);
    }

    draggedItem() {
        return this.pointer?.moved ? this.pointer.item : null;
    }

    pointerDown(event) {
        this.canvas.setPointerCapture(event.pointerId);
        const item = this.hooks.hit?.(event.offsetX, event.offsetY) ?? null;
        this.pointer = {x: event.offsetX, y: event.offsetY, east: this.view.east, north: this.view.north, item, moved: false};
        if (item !== null) {
            this.hooks.press?.(item);
        }
    }

    pointerMove(event) {
        const grid = this.toGrid(event.offsetX, event.offsetY);
        this.hooks.hover?.(event.offsetX, event.offsetY, grid, Boolean(this.pointer));
        const pointer = this.pointer;
        if (!pointer) {
            return;
        }
        if (!pointer.moved && Math.hypot(event.offsetX - pointer.x, event.offsetY - pointer.y) < MapView.dragThreshold) {
            return;
        }
        pointer.moved = true;
        this.canvas.dataset.dragging = "";
        if (pointer.item !== null && this.hooks.drag) {
            this.hooks.drag(pointer.item, grid);
        } else {
            const scale = this.view.metresPerPixel;
            this.view.east = pointer.east - (event.offsetX - pointer.x) * scale;
            this.view.north = pointer.north + (event.offsetY - pointer.y) * scale;
        }
        this.scheduleRender();
    }

    pointerUp(event) {
        const pointer = this.pointer;
        this.pointer = null;
        delete this.canvas.dataset.dragging;
        if (!pointer) {
            return;
        }
        if (pointer.moved) {
            if (pointer.item !== null) {
                this.hooks.dragEnd?.(pointer.item);
            }
            return;
        }
        this.hooks.click?.(event.offsetX, event.offsetY, pointer.item);
    }

    wheel(event) {
        event.preventDefault();
        const pixels = event.deltaMode === WheelEvent.DOM_DELTA_LINE ? event.deltaY * 40 : event.deltaMode === WheelEvent.DOM_DELTA_PAGE ? event.deltaY * 400 : event.deltaY;
        this.zoomAround(event.offsetX, event.offsetY, 2 ** (pixels / (event.ctrlKey ? MapView.pinchPixelsPerDoubling : MapView.wheelPixelsPerDoubling)));
    }
}
