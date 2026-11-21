#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "icons.h"
#include "log.h"

namespace gismo {

namespace {

constexpr double nearby_refresh_metres = 25.0;
constexpr int category_tab_height = 26;
inline constexpr const wchar_t *category_names[] = {L"All", L"Play", L"History", L"Nature", L"Sights"};
constexpr int category_tab_count = sizeof(category_names) / sizeof(category_names[0]);

RECT category_tab_rect(int tab) {
    int top = layout.bar_height + 28;
    int width = app.ui.width / category_tab_count;
    return {tab * width, top, tab == category_tab_count - 1 ? app.ui.width : (tab + 1) * width, top + category_tab_height};
}

void set_category(int category) {
    app.explore.category = category;
    app.mission.list_selected = 0;
    app.mission.list_scroll = 0;
    refresh_nearby(true);
    redraw();
}

}

void load_places() {
    if (app.explore.files_tried) {
        return;
    }
    app.explore.files_tried = true;
    app.explore.category = -1;
    wchar_t folder[MAX_PATH];
    find_data_folder(L"Maps", folder);
    wchar_t pattern[MAX_PATH];
    wsprintfW(pattern, L"%s\\*.gplaces", folder);
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) {
        log_line(L"no places in %s", folder);
        return;
    }
    do {
        wchar_t path[MAX_PATH];
        wsprintfW(path, L"%s\\%s", folder, found.cFileName);
        place_index &places = app.explore.files[app.explore.file_count];
        bool loaded = places.load(_wfopen(path, L"rb"));
        log_line(L"places %s: %s, %u places", found.cFileName, loaded ? L"loaded" : L"failed", places.count());
        if (loaded) {
            app.explore.file_count++;
        }
    } while (app.explore.file_count < max_place_files && FindNextFileW(search, &found));
    FindClose(search);
}

void refresh_nearby(bool force) {
    load_places();
    bool from_gps = app.location.gps_now.has_position;
    double east = from_gps ? app.location.position.easting : app.map.centre_east;
    double north = from_gps ? app.location.position.northing : app.map.centre_north;
    bool moved = grid_distance(east, north, app.explore.nearby_east, app.explore.nearby_north) > nearby_refresh_metres;
    if (!force && app.explore.nearby_valid && app.explore.nearby_category == app.explore.category && !moved) {
        return;
    }
    app.explore.nearby_count = 0;
    for (int file = 0; file < app.explore.file_count; file++) {
        app.explore.nearby_count = app.explore.files[file].nearest(east, north, app.explore.category, app.explore.nearby, app.explore.nearby_count, max_nearby);
    }
    app.explore.nearby_valid = true;
    app.explore.nearby_east = east;
    app.explore.nearby_north = north;
    app.explore.nearby_category = app.explore.category;
    app.explore.nearby_from_gps = from_gps;
    if (app.mission.list_selected >= app.explore.nearby_count) {
        app.mission.list_selected = app.explore.nearby_count > 0 ? app.explore.nearby_count - 1 : 0;
    }
    keep_selection_visible();
}

COLORREF category_colour(int category) {
    switch (category) {
    case 0:
        return RGB(255, 120, 0);
    case 1:
        return RGB(150, 90, 40);
    case 2:
        return RGB(40, 150, 60);
    case 3:
        return RGB(120, 60, 200);
    default:
        return RGB(30, 90, 200);
    }
}

const wchar_t *compass_point(double degrees) {
    static const wchar_t *points[] = {L"N", L"NE", L"E", L"SE", L"S", L"SW", L"W", L"NW"};
    int index = int((degrees + 22.5) / 45.0) % 8;
    return points[index < 0 ? index + 8 : index];
}

bool place_visited(const place_index *source, uint32_t index) {
    for (int visit = 0; visit < app.explore.visited_count; visit++) {
        if (app.explore.visited_source[visit] == source && app.explore.visited_index[visit] == index) {
            return true;
        }
    }
    return false;
}

void draw_nearby_screen() {
    refresh_nearby(false);
    for (int tab = 0; tab < category_tab_count; tab++) {
        RECT area = category_tab_rect(tab);
        bool selected = tab - 1 == app.explore.category;
        RECT inner = {area.left + 1, area.top + 2, area.right - 1, area.bottom - 2};
        fill(inner, selected ? category_colour(tab - 1) : RGB(235, 230, 215));
        text(category_names[tab], inner, selected ? RGB(255, 255, 255) : RGB(80, 70, 50), app.ui.small_font, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    RECT note = {6, layout.bar_height + 28 + category_tab_height, app.ui.width - 6, list_top() - 2};
    text(app.explore.nearby_from_gps ? L"Nearest to you" : L"Near the middle of the map (no GPS fix yet)", note, RGB(110, 100, 80), app.ui.small_font, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (app.explore.nearby_count == 0) {
        RECT message = {10, list_top() + 20, app.ui.width - 10, app.ui.height - layout.button_height - 10};
        text(app.explore.file_count ? L"Nothing like that nearby." : L"No places on the card.\nTap the folder above to choose a mission.", message, RGB(0, 0, 0), app.ui.medium_font, DT_CENTER | DT_WORDBREAK);
        return;
    }
    int bottom = app.ui.height - layout.button_height;
    for (int index = app.mission.list_scroll; index < app.explore.nearby_count; index++) {
        int top = list_top() + (index - app.mission.list_scroll) * layout.list_row_height;
        if (top >= bottom) {
            break;
        }
        const nearby_place &place = app.explore.nearby[index];
        bool is_target = app.explore.active && app.explore.spot.easting == place.source->east(place.index) && app.explore.spot.northing == place.source->north(place.index);
        RECT row = {0, top, app.ui.width, top + layout.list_row_height - 2};
        fill(row, index == app.mission.list_selected ? RGB(200, 225, 255) : is_target ? RGB(255, 235, 200) : RGB(255, 255, 255));
        if (index == app.mission.list_selected) {
            RECT marker = {0, top, 3, top + layout.list_row_height - 2};
            fill(marker, RGB(30, 90, 200));
        }
        int centre_y = top + layout.list_row_height / 2 - 1;
        circle(18, centre_y, 11, RGB(255, 255, 255), 2, category_colour(place.source->category(place.index)), true);
        if (place_visited(place.source, place.index)) {
            draw_icon(icon_check, 18, centre_y, RGB(255, 255, 255));
        }
        wchar_t distance[16];
        format_distance(place.distance, distance);
        double bearing = grid_bearing(app.explore.nearby_east, app.explore.nearby_north, place.source->east(place.index), place.source->north(place.index));
        wchar_t where[32];
        wsprintfW(where, L"%s %s", distance, compass_point(bearing));
        RECT where_area = {0, top, app.ui.width - 6, top + layout.list_row_height - 2};
        RECT measured = measure_text(where, where_area, app.ui.small_font, DT_SINGLELINE);
        where_area.left = where_area.right - (measured.right - measured.left);
        text(where, where_area, RGB(90, 90, 90), app.ui.small_font, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        int text_right = where_area.left - layout.list_text_gap;
        RECT name = {36, top + 1, text_right, top + 18};
        text_utf8(place.source->name(place.index), name, RGB(30, 30, 30), app.ui.medium_font, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        RECT kind = {36, top + 17, text_right, top + layout.list_row_height - 2};
        text_utf8(place.source->kind_label(place.index), kind, RGB(110, 100, 80), app.ui.small_font, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    int visible = visible_list_rows();
    if (app.explore.nearby_count > visible) {
        int track_top = list_top();
        int track_height = bottom - track_top;
        int thumb_top = track_top + track_height * app.mission.list_scroll / app.explore.nearby_count;
        RECT thumb = {app.ui.width - 4, thumb_top, app.ui.width, thumb_top + track_height * visible / app.explore.nearby_count};
        fill(thumb, RGB(150, 150, 170));
    }
}

void tap_nearby(int x, int y) {
    for (int tab = 0; tab < category_tab_count; tab++) {
        RECT area = category_tab_rect(tab);
        POINT tap = {x, y};
        if (PtInRect(&area, tap)) {
            set_category(tab - 1);
            return;
        }
    }
    if (y >= list_top()) {
        choose_nearby(app.mission.list_scroll + (y - list_top()) / layout.list_row_height);
    }
}

void key_nearby(int key) {
    if (key == VK_UP && app.mission.list_selected > 0) {
        app.mission.list_selected--;
        keep_selection_visible();
        redraw();
    } else if (key == VK_DOWN && app.mission.list_selected < app.explore.nearby_count - 1) {
        app.mission.list_selected++;
        keep_selection_visible();
        redraw();
    } else if (key == VK_LEFT || key == VK_RIGHT) {
        int category = app.explore.category + (key == VK_RIGHT ? 1 : -1);
        if (category < -1) {
            category = place_category_count - 1;
        } else if (category >= place_category_count) {
            category = -1;
        }
        set_category(category);
    } else if (is_select_key(key)) {
        if (app.explore.nearby_count > 0) {
            choose_nearby(app.mission.list_selected);
        } else {
            open_picker();
        }
    }
}

void choose_nearby(int index) {
    if (index < 0 || index >= app.explore.nearby_count) {
        return;
    }
    const nearby_place &place = app.explore.nearby[index];
    waypoint &spot = app.explore.spot;
    memset(&spot, 0, sizeof(spot));
    snprintf(spot.name, sizeof(spot.name), "%s", place.source->name(place.index));
    snprintf(spot.prompt, sizeof(spot.prompt), "%s", place.source->kind_label(place.index));
    spot.easting = place.source->east(place.index);
    spot.northing = place.source->north(place.index);
    spot.radius = 25;
    spot.task = task_kind::visit;
    spot.answer = -1;
    spot.response = -1;
    app.explore.spot_category = place.source->category(place.index);
    app.explore.active = true;
    app.explore.serial++;
    app.mission.list_selected = index;
    app.mission.warmth = 0;
    app.mission.warmth_tick = 0;
    app.map.route_valid = false;
    app.outing.events.add(app.location.gps_now, "explore target", spot.name, spot.prompt);
    app.ui.current = screen::map;
    app.map.follow = false;
    app.map.centre_east = spot.easting;
    app.map.centre_north = spot.northing;
    update_route();
    redraw();
}

void clear_explore_target() {
    app.explore.active = false;
    app.map.route_valid = false;
}

void open_found() {
    const nearby_place *match = nullptr;
    for (int index = 0; index < app.explore.nearby_count && match == nullptr; index++) {
        const nearby_place &place = app.explore.nearby[index];
        if (place.source->east(place.index) == app.explore.spot.easting && place.source->north(place.index) == app.explore.spot.northing) {
            match = &place;
        }
    }
    if (match && app.explore.visited_count < max_visited && !place_visited(match->source, match->index)) {
        app.explore.visited_source[app.explore.visited_count] = match->source;
        app.explore.visited_index[app.explore.visited_count] = match->index;
        app.explore.visited_count++;
    }
    app.outing.events.add(app.location.gps_now, "visited", app.explore.spot.name, app.explore.spot.prompt);
    app.explore.active = false;
    app.map.route_valid = false;
    app.ui.current = screen::found;
    MessageBeep(MB_OK);
    redraw();
}

void draw_found_screen() {
    RECT all = {0, 0, app.ui.width, app.ui.height};
    fill(all, category_colour(app.explore.spot_category));
    draw_icon(icon_star, app.ui.width / 2, layout.bar_height + 50, RGB(255, 230, 120));
    RECT banner = {0, layout.bar_height + 70, app.ui.width, layout.bar_height + 110};
    text(L"You found it!", banner, RGB(255, 255, 255), app.ui.huge_font, DT_CENTER | DT_SINGLELINE);
    RECT name = {10, layout.bar_height + 116, app.ui.width - 10, layout.bar_height + 176};
    text_utf8(app.explore.spot.name, name, RGB(255, 255, 255), app.ui.large_font, DT_CENTER | DT_WORDBREAK | DT_END_ELLIPSIS);
    RECT kind = {10, layout.bar_height + 178, app.ui.width - 10, layout.bar_height + 198};
    text_utf8(app.explore.spot.prompt, kind, RGB(255, 240, 200), app.ui.medium_font, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    rounded_button(task_button_rect(0, 1), L"Find another", RGB(30, 60, 110), app.ui.large_font);
}

}
