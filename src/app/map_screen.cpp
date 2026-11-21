#include "app.h"

#include <math.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "draw.h"
#include "icons.h"
#include "log.h"

namespace gismo {

namespace {

constexpr int max_route_nodes = 16384;
constexpr double max_route_snap_metres = 150.0;
constexpr double overview_threshold = 10.0;

}

double view_metres_per_pixel() {
    return zoom_levels[app.map.zoom];
}

map_view current_view() {
    return {app.map.centre_east, app.map.centre_north, view_metres_per_pixel()};
}

framebuffer current_framebuffer() {
    return {app.ui.bits, app.ui.width, app.ui.height, app.ui.stride};
}

void load_maps() {
    wchar_t folder[MAX_PATH];
    find_data_folder(L"Maps", folder);
    wchar_t pattern[MAX_PATH];
    wsprintfW(pattern, L"%s\\*.gmap", folder);
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) {
        log_line(L"no maps in %s", folder);
        return;
    }
    do {
        if (app.map.pack_count >= max_packs) {
            break;
        }
        loaded_pack &entry = app.map.packs[app.map.pack_count];
        wchar_t path[MAX_PATH];
        wsprintfW(path, L"%s\\%s", folder, found.cFileName);
        if (!entry.pack.open(_wfopen(path, L"rb"))) {
            log_line(L"can't open map %s", path);
            continue;
        }
        entry.cache = new map_tile_cache(20);
        entry.finest = entry.pack.level(0).millimetres_per_pixel / 1000.0;
        entry.coarsest = entry.pack.level(entry.pack.level_count() - 1).millimetres_per_pixel / 1000.0;
        entry.overview = entry.finest >= overview_threshold;
        utf8_to_wide(entry.pack.name(), entry.name, 64);
        if (entry.name[0] == 0) {
            lstrcpynW(entry.name, found.cFileName, 64);
        }
        log_line(L"map %s: %s, %d levels, %d mm/px", found.cFileName, entry.name, entry.pack.level_count(), int(entry.finest * 1000));
        if (entry.overview) {
            if (app.map.overview_pack < 0) {
                app.map.overview_pack = app.map.pack_count;
            }
        } else {
            app.map.detail_packs[app.map.detail_count++] = app.map.pack_count;
        }
        app.map.pack_count++;
    } while (FindNextFileW(search, &found));
    FindClose(search);

    app.map.layer = 0;
    for (int index = 1; index < app.map.detail_count; index++) {
        if (lstrlenW(app.map.packs[app.map.detail_packs[index]].name) < lstrlenW(app.map.packs[app.map.detail_packs[app.map.layer]].name)) {
            app.map.layer = index;
        }
    }
    const loaded_pack *centre_pack = app.map.detail_count ? &app.map.packs[app.map.detail_packs[app.map.layer]] : app.map.pack_count ? &app.map.packs[0] : nullptr;
    if (centre_pack) {
        const map_level &level = centre_pack->pack.level(0);
        app.map.centre_east = level.west + level.columns * level.span() / 2.0;
        app.map.centre_north = level.north - level.rows * level.span() / 2.0;
    }
}

bool draw_layers_into(framebuffer target, const map_view &view) {
    bool drawn = false;
    if (app.map.overview_pack >= 0) {
        loaded_pack &overview = app.map.packs[app.map.overview_pack];
        draw_map(overview.pack, *overview.cache, view, target, map_fill::background, 0xE71C);
        drawn = true;
    }
    if (app.map.detail_count) {
        loaded_pack &detail = app.map.packs[app.map.detail_packs[app.map.layer]];
        if (!drawn || view.metres_per_pixel <= detail.coarsest * 2) {
            draw_map(detail.pack, *detail.cache, view, target, drawn ? map_fill::keep : map_fill::background, 0xE71C);
            drawn = true;
        }
    }
    return drawn;
}

void draw_layers() {
    if (!draw_layers_into(current_framebuffer(), current_view())) {
        RECT all = {0, 0, app.ui.width, app.ui.height};
        fill(all, RGB(230, 230, 220));
        RECT message = {10, layout.bar_height + 20, app.ui.width - 10, app.ui.height - layout.button_height - 10};
        text(L"No maps found.\nPut .gmap files in\n\\Storage Card\\GISmo\\Maps", message, RGB(0, 0, 0), app.ui.medium_font, DT_CENTER | DT_WORDBREAK);
    }
}

bool route_tile_core(const wchar_t *name, double &west, double &south, double &size) {
    const wchar_t *end = wcsrchr(name, L'.');
    if (end == nullptr) {
        return false;
    }
    int values[3];
    for (int part = 2; part >= 0; part--) {
        const wchar_t *start = end;
        int value = 0;
        int multiplier = 1;
        while (start > name && start[-1] >= L'0' && start[-1] <= L'9') {
            start--;
            value += (*start - L'0') * multiplier;
            multiplier *= 10;
        }
        if (start == end || start == name || start[-1] != L'-') {
            return false;
        }
        values[part] = value;
        end = start - 1;
    }
    west = values[0] * 1000.0;
    south = values[1] * 1000.0;
    size = values[2] * 1000.0;
    return size > 0;
}

int64_t nearest_route_node(double east, double north) {
    int64_t node = app.map.routes.nearest(east, north);
    if (node < 0) {
        return -1;
    }
    const route_node &position = app.map.routes.node(uint32_t(node));
    return grid_distance(position.east / 10.0, position.north / 10.0, east, north) <= max_route_snap_metres ? node : -1;
}

bool try_route_file(const wchar_t *folder, const wchar_t *name, const waypoint *goal) {
    wchar_t path[MAX_PATH];
    wsprintfW(path, L"%s\\%s", folder, name);
    DWORD started = GetTickCount();
    bool loaded = app.map.routes.load(_wfopen(path, L"rb"));
    bool suitable = loaded && (goal == nullptr || (app.map.routes.covers(goal->easting, goal->northing) && nearest_route_node(goal->easting, goal->northing) >= 0));
    log_line(L"route graph %s: %s, %u nodes in %u ms", name, !loaded ? L"failed" : suitable ? L"loaded" : L"doesn't cover the target", app.map.routes.nodes(), GetTickCount() - started);
    return suitable;
}

bool load_routes() {
    const waypoint *goal = current_target();
    if (app.map.routes.loaded() && (goal == nullptr || app.map.routes.covers(goal->easting, goal->northing))) {
        return true;
    }
    if (app.map.routes_tried && app.map.routes_tried_target == target_key()) {
        return app.map.routes.loaded();
    }
    app.map.routes_tried = true;
    app.map.routes_tried_target = target_key();
    if (app.map.route_nodes == nullptr) {
        app.map.route_nodes = static_cast<uint32_t *>(malloc(max_route_nodes * sizeof(uint32_t)));
        app.map.route = {app.map.route_nodes, 0, app.map.route_nodes ? max_route_nodes : 0, 0, 0};
    }
    app.map.route_goal_node = -1;
    app.map.route_valid = false;
    double aim_east = goal ? goal->easting : app.location.position.easting;
    double aim_north = goal ? goal->northing : app.location.position.northing;
    bool have_aim = goal != nullptr || app.location.gps_now.has_position;
    wchar_t folder[MAX_PATH];
    find_data_folder(L"Maps", folder);
    wchar_t pattern[MAX_PATH];
    wsprintfW(pattern, L"%s\\*.groute", folder);
    bool suitable = false;
    for (int pass = 0; pass < 2 && !suitable; pass++) {
        WIN32_FIND_DATAW found;
        HANDLE search = FindFirstFileW(pattern, &found);
        if (search == INVALID_HANDLE_VALUE) {
            log_line(L"no route graph in %s", folder);
            return false;
        }
        do {
            double west, south, size;
            bool tile = route_tile_core(found.cFileName, west, south, size);
            if (pass == 0) {
                if (tile && have_aim && aim_east >= west && aim_east < west + size && aim_north >= south && aim_north < south + size) {
                    suitable = try_route_file(folder, found.cFileName, goal);
                }
            } else {
                suitable = try_route_file(folder, found.cFileName, goal);
            }
        } while (!suitable && FindNextFileW(search, &found));
        FindClose(search);
    }
    if (!suitable) {
        app.map.routes.load(nullptr);
    }
    return suitable && app.map.route_nodes;
}

bool route_shown() {
    const waypoint *point = current_target();
    return point && (app.map.route_hint || point->route);
}

void update_route() {
    const waypoint *point = current_target();
    int key = target_key();
    if (route_shown() && app.map.route_target != key) {
        load_routes();
    }
    if (!route_shown() || !app.location.gps_now.has_position || !app.map.routes.loaded()) {
        app.map.route_valid = false;
        return;
    }
    bool moved = grid_distance(app.map.route_origin_east, app.map.route_origin_north, app.location.position.easting, app.location.position.northing) > app.settings.route_refresh_metres;
    if (app.map.route_valid && app.map.route_target == key && !moved) {
        return;
    }
    DWORD started = GetTickCount();
    if (app.map.route_target != key || app.map.route_goal_node < 0) {
        app.map.route_goal_node = nearest_route_node(point->easting, point->northing);
    }
    int64_t start = nearest_route_node(app.location.position.easting, app.location.position.northing);
    app.map.route_target = key;
    app.map.route_origin_east = app.location.position.easting;
    app.map.route_origin_north = app.location.position.northing;
    app.map.route_valid = start >= 0 && app.map.route_goal_node >= 0 && app.map.routes.find(uint32_t(start), uint32_t(app.map.route_goal_node), app.map.route);
    wchar_t name[48];
    utf8_to_wide(point->name, name, 48);
    log_line(L"route to %s: %s, %d nodes, %d m, %u ms", name, app.map.route_valid ? L"found" : L"none", app.map.route.count, int(app.map.route.length_metres), GetTickCount() - started);
}

double route_distance_to_target() {
    if (!app.map.route_valid || app.map.route.count == 0) {
        return -1;
    }
    const route_node &first = app.map.routes.node(app.map.route.nodes[0]);
    const route_node &last = app.map.routes.node(app.map.route.nodes[app.map.route.count - 1]);
    const waypoint *point = current_target();
    if (point == nullptr) {
        return -1;
    }
    return app.map.route.length_metres + grid_distance(app.location.position.easting, app.location.position.northing, first.east / 10.0, first.north / 10.0) + grid_distance(last.east / 10.0, last.north / 10.0, point->easting, point->northing);
}

void draw_route() {
    const waypoint *goal = current_target();
    if (!route_shown() || !app.map.route_valid || app.map.route.count == 0 || goal == nullptr) {
        return;
    }
    const waypoint &point = *goal;
    map_view view = current_view();
    framebuffer target = current_framebuffer();
    for (int pass = 0; pass < 2; pass++) {
        int32_t width = pass == 0 ? 7 * subpixel : 4 * subpixel;
        uint16_t colour = pass == 0 ? 0xFFFF : to_565(RGB(0, 110, 255));
        int x;
        int y;
        map_to_screen(view, target, app.location.position.easting, app.location.position.northing, x, y);
        int previous_x = x;
        int previous_y = y;
        for (int index = 0; index <= app.map.route.count; index++) {
            if (index < app.map.route.count) {
                const route_node &node = app.map.routes.node(app.map.route.nodes[index]);
                map_to_screen(view, target, node.east / 10.0, node.north / 10.0, x, y);
            } else {
                map_to_screen(view, target, point.easting, point.northing, x, y);
            }
            bool visible = !((x < -8 && previous_x < -8) || (y < -8 && previous_y < -8) || (x > app.ui.width + 8 && previous_x > app.ui.width + 8) || (y > app.ui.height + 8 && previous_y > app.ui.height + 8));
            if (visible && (x != previous_x || y != previous_y)) {
                draw_line(target, {previous_x * subpixel + subpixel / 2, previous_y * subpixel + subpixel / 2}, {x * subpixel + subpixel / 2, y * subpixel + subpixel / 2}, width, colour);
            }
            previous_x = x;
            previous_y = y;
        }
    }
}

void draw_trail() {
    if (app.map.trail_count < 2) {
        return;
    }
    map_view view = current_view();
    framebuffer target = current_framebuffer();
    uint16_t colour = to_565(RGB(220, 30, 120));
    int previous_x = 0;
    int previous_y = 0;
    for (int index = 0; index < app.map.trail_count; index++) {
        int x;
        int y;
        map_to_screen(view, target, app.map.trail[index].east, app.map.trail[index].north, x, y);
        if (index > 0) {
            bool visible = !((x < -4 && previous_x < -4) || (y < -4 && previous_y < -4) || (x > app.ui.width + 4 && previous_x > app.ui.width + 4) || (y > app.ui.height + 4 && previous_y > app.ui.height + 4));
            if (visible) {
                draw_line(target, {previous_x * subpixel + subpixel / 2, previous_y * subpixel + subpixel / 2}, {x * subpixel + subpixel / 2, y * subpixel + subpixel / 2}, 3 * subpixel, colour);
            }
        }
        previous_x = x;
        previous_y = y;
    }
}

void draw_explore_spot() {
    if (!app.explore.active) {
        return;
    }
    map_view view = current_view();
    framebuffer target = current_framebuffer();
    int x;
    int y;
    map_to_screen(view, target, app.explore.spot.easting, app.explore.spot.northing, x, y);
    if (x < -20 || y < -20 || x > app.ui.width + 20 || y > app.ui.height + 20) {
        return;
    }
    int area = int(app.explore.spot.radius / view.metres_per_pixel);
    if (area > 14) {
        circle(x, y, area, RGB(255, 140, 0), 2, 0, false);
    }
    circle(x, y, 12, RGB(255, 255, 255), 2, category_colour(app.explore.spot_category), true);
    draw_icon(icon_star, x, y, RGB(255, 255, 255));
}

void draw_waypoints() {
    if (!app.mission.have_mission) {
        draw_explore_spot();
        return;
    }
    map_view view = current_view();
    framebuffer target = current_framebuffer();
    for (int index = 0; index < app.mission.quest.count; index++) {
        const waypoint &point = app.mission.quest.waypoints[index];
        int x;
        int y;
        map_to_screen(view, target, point.easting, point.northing, x, y);
        if (x < -20 || y < -20 || x > app.ui.width + 20 || y > app.ui.height + 20) {
            continue;
        }
        bool is_target = index == app.mission.target;
        bool via = point.task == task_kind::via;
        int radius = via ? (is_target ? 7 : 5) : is_target ? 12 : 9;
        int area = int(point.radius / view.metres_per_pixel);
        if (is_target && area > radius + 2) {
            circle(x, y, area, RGB(255, 140, 0), 2, 0, false);
        }
        COLORREF colour = point.done ? RGB(40, 170, 70) : is_target ? RGB(255, 120, 0) : RGB(120, 60, 200);
        circle(x, y, radius, RGB(255, 255, 255), 2, colour, true);
        if (via) {
            continue;
        }
        if (point.done) {
            draw_icon(icon_check, x, y, RGB(255, 255, 255));
        } else {
            wchar_t label[8];
            wsprintfW(label, L"%d", stop_number(app.mission.quest, index));
            RECT label_area = {x - radius, y - radius, x + radius + 1, y + radius + 1};
            text(label, label_area, RGB(255, 255, 255), app.ui.small_font, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }
}

void draw_position() {
    if (!app.location.gps_now.has_position) {
        return;
    }
    map_view view = current_view();
    framebuffer target = current_framebuffer();
    int x;
    int y;
    map_to_screen(view, target, app.location.position.easting, app.location.position.northing, x, y);
    int accuracy = int(position_error_metres(app.location.gps_now) / view.metres_per_pixel);
    if (accuracy > 8) {
        circle(x, y, accuracy, RGB(0, 120, 255), 1, 0, false);
    }
    if (app.location.gps_now.has_course && app.location.gps_now.speed_metres_per_second > 0.5) {
        arrow(x, y, 11, app.location.gps_now.course_degrees, RGB(0, 120, 255));
    } else {
        circle(x, y, 7, RGB(255, 255, 255), 2, RGB(0, 120, 255), true);
    }
}

void draw_target_strip() {
    const waypoint *goal = current_target();
    if (goal == nullptr) {
        return;
    }
    const waypoint &point = *goal;
    int top = app.ui.height - layout.button_height - layout.strip_height;
    RECT strip = {0, top, app.ui.width, top + layout.strip_height};
    fill(strip, RGB(255, 245, 220));
    RECT edge = {0, top, app.ui.width, top + 2};
    fill(edge, RGB(255, 140, 0));
    wchar_t name[48];
    utf8_to_wide(point.name, name, 48);
    wchar_t line[96];
    if (app.mission.have_mission && point.task == task_kind::via) {
        lstrcpyW(line, L"Go this way");
    } else if (app.mission.have_mission) {
        wsprintfW(line, L"%d. %s", stop_number(app.mission.quest, app.mission.target), name);
    } else {
        lstrcpyW(line, name);
    }
    RECT name_area = {38, top + 2, app.ui.width - 4, top + 19};
    if (!app.location.gps_now.has_position) {
        draw_icon(icon_flag, 20, top + layout.strip_height / 2 + 1, RGB(255, 120, 0));
        name_area.bottom = top + layout.strip_height;
        text(line, name_area, RGB(60, 30, 0), app.ui.medium_font, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return;
    }
    double distance = grid_distance(app.location.position.easting, app.location.position.northing, point.easting, point.northing);
    double walking = route_distance_to_target();
    if (walking > distance) {
        distance = walking;
    }
    double bearing = grid_bearing(app.location.position.easting, app.location.position.northing, point.easting, point.northing);
    arrow(20, top + layout.strip_height / 2 + 1, 13, bearing, RGB(255, 120, 0));
    text(line, name_area, RGB(60, 30, 0), app.ui.medium_font, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

    wchar_t distance_text[16];
    format_distance(distance, distance_text);
    wchar_t detail[64];
    int steps = int(distance / app.settings.metres_per_step / 10 + 0.5) * 10;
    if (distance < 30) {
        wsprintfW(detail, L"%s  Really close!", distance_text);
    } else {
        wsprintfW(detail, L"%s  about %d steps", distance_text, steps);
    }
    draw_icon(icon_directions_walk, 46, top + 27, RGB(120, 70, 20));
    RECT detail_area = {58, top + 19, app.ui.width - 70, top + layout.strip_height};
    text(detail, detail_area, RGB(120, 70, 20), app.ui.small_font, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (app.mission.warmth != 0) {
        RECT warmth_area = {app.ui.width - 74, top + 19, app.ui.width - 6, top + layout.strip_height};
        text(app.mission.warmth > 0 ? L"Warmer!" : L"Colder\x2026", warmth_area, app.mission.warmth > 0 ? RGB(220, 60, 20) : RGB(30, 90, 200), app.ui.small_font, DT_RIGHT | DT_SINGLELINE);
    }
}

void draw_pan_badge() {
    if (!app.map.pan_mode) {
        return;
    }
    RECT badge = {6, layout.bar_height + 6, 62, layout.bar_height + 28};
    fill(badge, RGB(30, 90, 200));
    text(L"PAN", badge, RGB(255, 255, 255), app.ui.medium_font, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void set_pan_mode(bool on) {
    app.map.pan_mode = on;
    app.map.pan_mode_tick = GetTickCount();
    if (on) {
        SetTimer(app.ui.window, pan_mode_timer, 250, nullptr);
    } else {
        KillTimer(app.ui.window, pan_mode_timer);
    }
    InvalidateRect(app.ui.window, nullptr, FALSE);
}

void check_pan_mode() {
    if (app.map.pan_mode && GetTickCount() - app.map.pan_mode_tick > milliseconds(app.settings.pan_mode_timeout_seconds)) {
        set_pan_mode(false);
    }
}

void draw_map_screen() {
    draw_layers();
    draw_trail();
    draw_route();
    draw_waypoints();
    draw_position();
    draw_target_strip();
    draw_pan_badge();
    if (app.map.detail_count > 1) {
        draw_floating_button(button::layer, icon_layers, app.map.layer != 0);
    }
    draw_floating_button(button::follow, icon_my_location, app.map.follow);
    if (current_target()) {
        draw_floating_button(button::route, icon_route, route_shown());
    }
}

void show_map_if_overlay() {
    if (app.ui.current == screen::sky || app.ui.current == screen::list) {
        app.ui.current = screen::map;
    }
}

void zoom_by(int steps) {
    app.map.zoom += steps;
    if (app.map.zoom < 0) {
        app.map.zoom = 0;
    }
    if (app.map.zoom > zoom_count - 1) {
        app.map.zoom = zoom_count - 1;
    }
    redraw();
}

void pan_by(int columns, int rows) {
    app.map.follow = false;
    app.map.centre_east += columns * app.ui.width / 4 * view_metres_per_pixel();
    app.map.centre_north -= rows * app.ui.height / 4 * view_metres_per_pixel();
    redraw();
}

}
