#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bitmap_file.h"
#include "draw.h"
#include "icons.h"
#include "log.h"

namespace gismo {

namespace {

constexpr int max_count_value = 999;
constexpr int task_gap = 6;
constexpr int choice_height = 40;
constexpr int min_choice_height = 28;
constexpr int feedback_height = 22;
constexpr int count_row_height = 56;
constexpr int count_button_width = 60;

}

int list_top() {
    return layout.bar_height + 28 + layout.clue_box_height;
}

int visible_list_rows() {
    return (app.ui.height - layout.button_height - list_top()) / layout.list_row_height;
}

int list_count() {
    return app.mission.have_mission ? app.mission.quest.count : app.explore.nearby_count;
}

int max_list_scroll() {
    int limit = list_count() - visible_list_rows();
    return limit > 0 ? limit : 0;
}

void keep_selection_visible() {
    if (app.mission.list_selected < app.mission.list_scroll) {
        app.mission.list_scroll = app.mission.list_selected;
    }
    if (app.mission.list_selected >= app.mission.list_scroll + visible_list_rows()) {
        app.mission.list_scroll = app.mission.list_selected - visible_list_rows() + 1;
    }
    if (app.mission.list_scroll > max_list_scroll()) {
        app.mission.list_scroll = max_list_scroll();
    }
    if (app.mission.list_scroll < 0) {
        app.mission.list_scroll = 0;
    }
}

RECT mission_header_rect() {
    return {0, layout.bar_height, app.ui.width, layout.bar_height + 28};
}

RECT header_summary_rect() {
    RECT header = mission_header_rect();
    return {app.ui.width - 66, header.top, app.ui.width - 34, header.bottom};
}

void draw_mission_header(const wchar_t *fallback) {
    RECT header = mission_header_rect();
    fill(header, RGB(235, 228, 250));
    RECT summary = header_summary_rect();
    RECT title = {8, header.top, summary.left - 4, header.bottom};
    if (app.mission.have_mission) {
        text_utf8(app.mission.quest.title, title, RGB(120, 60, 200), app.ui.large_font, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    } else {
        text(fallback, title, RGB(120, 60, 200), app.ui.large_font, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    draw_icon(icon_directions_walk, (summary.left + summary.right) / 2, (header.top + header.bottom) / 2, RGB(120, 60, 200));
    draw_icon(icon_folder_open, app.ui.width - 18, (header.top + header.bottom) / 2, RGB(120, 60, 200));
}

void draw_list_screen() {
    RECT all = {0, 0, app.ui.width, app.ui.height};
    fill(all, RGB(250, 248, 240));
    draw_mission_header(L"Exploring");
    if (!app.mission.have_mission) {
        if (app.mission.mission_error[0]) {
            RECT message = {10, layout.bar_height + 40, app.ui.width - 10, app.ui.height - layout.button_height - 10};
            text(app.mission.mission_error, message, RGB(0, 0, 0), app.ui.medium_font, DT_CENTER | DT_WORDBREAK);
            return;
        }
        draw_nearby_screen();
        return;
    }
    RECT clue_box = {0, layout.bar_height + 28, app.ui.width, list_top() - 2};
    fill(clue_box, RGB(255, 250, 225));
    const waypoint &selected = app.mission.quest.waypoints[app.mission.list_selected];
    draw_icon(icon_lightbulb, 16, clue_box.top + 16, RGB(230, 160, 0));
    RECT clue_text = {32, clue_box.top + 3, app.ui.width - 6, clue_box.bottom - 2};
    const char *clue = selected.clue[0] ? selected.clue : app.mission.quest.intro[0] ? app.mission.quest.intro : "Follow the arrow on the map!";
    text_utf8(clue, clue_text, RGB(80, 60, 0), app.ui.small_font, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);

    int bottom = app.ui.height - layout.button_height;
    for (int index = app.mission.list_scroll; index < app.mission.quest.count; index++) {
        int top = list_top() + (index - app.mission.list_scroll) * layout.list_row_height;
        if (top >= bottom) {
            break;
        }
        const waypoint &point = app.mission.quest.waypoints[index];
        RECT row = {0, top, app.ui.width, top + layout.list_row_height - 2};
        fill(row, index == app.mission.list_selected ? RGB(200, 225, 255) : index == app.mission.target ? RGB(255, 235, 200) : RGB(255, 255, 255));
        if (index == app.mission.list_selected) {
            RECT marker = {0, top, 3, top + layout.list_row_height - 2};
            fill(marker, RGB(30, 90, 200));
        }
        COLORREF colour = point.done ? RGB(40, 170, 70) : index == app.mission.target ? RGB(255, 120, 0) : RGB(120, 60, 200);
        bool via = point.task == task_kind::via;
        circle(18, top + layout.list_row_height / 2 - 1, via ? 5 : 11, RGB(255, 255, 255), 2, colour, true);
        if (!via && point.done) {
            draw_icon(icon_check, 18, top + layout.list_row_height / 2 - 1, RGB(255, 255, 255));
        } else if (!via) {
            wchar_t label[8];
            wsprintfW(label, L"%d", stop_number(app.mission.quest, index));
            RECT label_area = {7, top, 30, top + layout.list_row_height - 2};
            text(label, label_area, RGB(255, 255, 255), app.ui.small_font, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        int name_right = app.ui.width - 6;
        if (app.location.gps_now.has_position) {
            wchar_t distance[16];
            format_distance(grid_distance(app.location.position.easting, app.location.position.northing, point.easting, point.northing), distance);
            RECT distance_area = {0, top, app.ui.width - 6, top + layout.list_row_height - 2};
            RECT measured = measure_text(distance, distance_area, app.ui.small_font, DT_SINGLELINE);
            distance_area.left = distance_area.right - (measured.right - measured.left);
            text(distance, distance_area, RGB(90, 90, 90), app.ui.small_font, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            name_right = distance_area.left - layout.list_text_gap;
        }
        RECT name = {36, top, name_right, top + layout.list_row_height - 2};
        if (via) {
            text(L"Go this way", name, RGB(130, 120, 110), app.ui.small_font, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        } else {
            text_utf8(point.name, name, RGB(30, 30, 30), app.ui.medium_font, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
    }
    int visible = visible_list_rows();
    if (app.mission.quest.count > visible) {
        int track_top = list_top();
        int track_height = bottom - track_top;
        int thumb_height = track_height * visible / app.mission.quest.count;
        int thumb_top = track_top + track_height * app.mission.list_scroll / app.mission.quest.count;
        RECT thumb = {app.ui.width - 4, thumb_top, app.ui.width, thumb_top + thumb_height};
        fill(thumb, RGB(150, 150, 170));
    }
}

int picker_rows() {
    return app.mission.mission_count + 1;
}

int picker_top() {
    return layout.bar_height + 28 + layout.preview_height;
}

int visible_picker_rows() {
    return (app.ui.height - layout.button_height - picker_top()) / layout.list_row_height;
}

void draw_mission_preview(int top) {
    RECT area = {0, top, app.ui.width, top + layout.preview_height};
    if (app.mission.picker_selected >= app.mission.mission_count || app.mission.missions[app.mission.picker_selected].points == 0) {
        fill(area, RGB(225, 230, 220));
        text(app.mission.picker_selected >= app.mission.mission_count ? L"Explore anywhere!" : L"This mission has a problem", area, RGB(90, 90, 90), app.ui.medium_font, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }
    const mission_entry &entry = app.mission.missions[app.mission.picker_selected];
    double west = entry.east[0];
    double east = west;
    double south = entry.north[0];
    double north = south;
    for (int point = 1; point < entry.points; point++) {
        west = entry.east[point] < west ? entry.east[point] : west;
        east = entry.east[point] > east ? entry.east[point] : east;
        south = entry.north[point] < south ? entry.north[point] : south;
        north = entry.north[point] > north ? entry.north[point] : north;
    }
    int zoom = 0;
    while (zoom < zoom_count - 1 && ((east - west) / zoom_levels[zoom] > app.ui.width - 40 || (north - south) / zoom_levels[zoom] > layout.preview_height - 30)) {
        zoom++;
    }
    map_view view = {(west + east) / 2, (south + north) / 2, zoom_levels[zoom]};
    framebuffer target = {app.ui.bits + top * app.ui.stride, app.ui.width, layout.preview_height, app.ui.stride};
    if (!draw_layers_into(target, view)) {
        fill(area, RGB(225, 230, 220));
    }
    for (int point = 0; point < entry.points; point++) {
        int x;
        int y;
        map_to_screen(view, target, entry.east[point], entry.north[point], x, y);
        COLORREF colour = entry.point_done[point] ? RGB(40, 170, 70) : RGB(120, 60, 200);
        circle(x, top + y, 8, RGB(255, 255, 255), 2, colour, true);
        wchar_t label[8];
        wsprintfW(label, L"%d", point + 1);
        RECT label_area = {x - 8, top + y - 8, x + 9, top + y + 9};
        text(label, label_area, RGB(255, 255, 255), app.ui.small_font, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    RECT edge = {0, area.bottom - 1, app.ui.width, area.bottom};
    fill(edge, RGB(120, 60, 200));
}

void keep_picker_visible() {
    int visible = visible_picker_rows();
    if (app.mission.picker_selected < app.mission.picker_scroll) {
        app.mission.picker_scroll = app.mission.picker_selected;
    }
    if (app.mission.picker_selected >= app.mission.picker_scroll + visible) {
        app.mission.picker_scroll = app.mission.picker_selected - visible + 1;
    }
    if (app.mission.picker_scroll < 0) {
        app.mission.picker_scroll = 0;
    }
}

void draw_missions_screen() {
    RECT all = {0, 0, app.ui.width, app.ui.height};
    fill(all, RGB(250, 248, 240));
    RECT header = mission_header_rect();
    fill(header, RGB(235, 228, 250));
    RECT title = {8, header.top, app.ui.width - 8, header.bottom};
    text(L"Choose a mission", title, RGB(120, 60, 200), app.ui.large_font, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    draw_mission_preview(header.bottom);

    int bottom = app.ui.height - layout.button_height;
    for (int index = app.mission.picker_scroll; index < picker_rows(); index++) {
        int top = picker_top() + (index - app.mission.picker_scroll) * layout.list_row_height;
        if (top >= bottom) {
            break;
        }
        bool current = index < app.mission.mission_count ? index == app.mission.mission_index : app.mission.mission_index < 0;
        RECT row = {0, top, app.ui.width, top + layout.list_row_height - 2};
        fill(row, index == app.mission.picker_selected ? RGB(200, 225, 255) : current ? RGB(255, 235, 200) : RGB(255, 255, 255));
        RECT name = {12, top, app.ui.width - 60, top + layout.list_row_height - 2};
        RECT progress = {app.ui.width - 60, top, app.ui.width - 8, top + layout.list_row_height - 2};
        if (index == app.mission.mission_count) {
            text(L"Just explore", name, RGB(30, 30, 30), app.ui.medium_font, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            continue;
        }
        const mission_entry &entry = app.mission.missions[index];
        text_utf8(entry.title, name, entry.valid ? RGB(30, 30, 30) : RGB(180, 40, 40), app.ui.medium_font, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        wchar_t status[24];
        if (!entry.valid) {
            lstrcpyW(status, L"error");
        } else if (entry.done == entry.count) {
            lstrcpyW(status, L"done!");
        } else {
            wsprintfW(status, L"%d/%d", entry.done, entry.count);
        }
        text(status, progress, entry.done == entry.count && entry.valid ? RGB(40, 150, 60) : RGB(90, 90, 90), app.ui.small_font, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
}

RECT task_button_rect(int index, int count) {
    int top = app.ui.height - 10 - (count - index) * 46;
    return {16, top, app.ui.width - 16, top + 40};
}

const char *task_prompt(const waypoint &point) {
    return point.prompt[0] ? point.prompt : "Well done!";
}

bool choice_labels_fit(const waypoint &point, int width) {
    for (int index = 0; index < point.choice_count; index++) {
        RECT area = {0, 0, width, 100};
        RECT measured = measure_text_utf8(point.choices[index], area, app.ui.large_font, DT_SINGLELINE);
        if (measured.right - measured.left > width - 12) {
            return false;
        }
    }
    return true;
}

task_layout layout_task_attempt(const waypoint &point, bool compact, bool columns, bool small_prompt) {
    task_layout result = {};
    int top = layout.bar_height + 6;
    if (!compact) {
        result.banner = {0, top, app.ui.width, top + 34};
        top += 36;
    }
    result.name = {10, top, app.ui.width - 10, top + 28};
    top += 34;
    int bottom = app.ui.height - 10;
    result.prompt_font = small_prompt ? app.ui.small_font : app.ui.medium_font;
    RECT available = {14, top, app.ui.width - 14, bottom};
    RECT measured = measure_text_utf8(task_prompt(point), available, result.prompt_font, DT_CENTER | DT_WORDBREAK);
    int prompt_bottom = top + (measured.bottom - measured.top);
    result.action = task_button_rect(0, 1);
    int prompt_limit = result.action.top - task_gap;
    if (point.task == task_kind::choice && point.choice_count > 0) {
        int count = point.choice_count;
        int per_row = columns ? 2 : 1;
        int rows = (count + per_row - 1) / per_row;
        int space = bottom - prompt_bottom - task_gap - feedback_height - (rows - 1) * task_gap;
        int height = space / rows;
        height = height > choice_height ? choice_height : height < min_choice_height ? min_choice_height : height;
        int buttons_top = bottom - rows * height - (rows - 1) * task_gap;
        int width = (app.ui.width - 32 - (per_row - 1) * task_gap) / per_row;
        for (int index = 0; index < count; index++) {
            int row = index / per_row;
            int column = index % per_row;
            int left = 16 + column * (width + task_gap);
            int button_top = buttons_top + row * (height + task_gap);
            result.choices[index] = {left, button_top, per_row == 1 ? app.ui.width - 16 : left + width, button_top + height};
        }
        result.feedback = {0, buttons_top - feedback_height, app.ui.width, buttons_top - 2};
        prompt_limit = result.feedback.top;
    } else if (point.task == task_kind::count) {
        int space = result.action.top - task_gap - prompt_bottom;
        int row_top = space >= count_row_height ? prompt_bottom + (space - count_row_height) / 2 : result.action.top - task_gap - count_row_height;
        result.minus = {16, row_top, 16 + count_button_width, row_top + count_row_height};
        result.plus = {app.ui.width - 16 - count_button_width, row_top, app.ui.width - 16, row_top + count_row_height};
        result.value = {result.minus.right, row_top, result.plus.left, row_top + count_row_height};
        prompt_limit = row_top - 2;
    }
    result.prompt = {14, top, app.ui.width - 14, prompt_limit};
    return result;
}

task_layout layout_task(const waypoint &point) {
    bool columns_possible = point.task == task_kind::choice && point.choice_count >= 4 && choice_labels_fit(point, (app.ui.width - 32 - task_gap) / 2);
    const bool attempts[][3] = {{false, false, false}, {false, true, false}, {true, true, false}, {true, true, true}};
    task_layout result = {};
    for (const auto &attempt : attempts) {
        bool columns = attempt[1] && columns_possible;
        result = layout_task_attempt(point, attempt[0], columns, attempt[2]);
        RECT measured = measure_text_utf8(task_prompt(point), result.prompt, result.prompt_font, DT_CENTER | DT_WORDBREAK);
        if (measured.bottom <= result.prompt.bottom) {
            break;
        }
    }
    return result;
}

void draw_task_screen() {
    const waypoint &point = app.mission.quest.waypoints[app.mission.task_waypoint];
    RECT all = {0, 0, app.ui.width, app.ui.height};
    fill(all, RGB(120, 60, 200));
    task_layout places = layout_task(point);
    if (places.banner.bottom > places.banner.top) {
        text(L"You found it!", places.banner, RGB(255, 220, 80), app.ui.huge_font, DT_CENTER | DT_SINGLELINE);
    }
    text_utf8(point.name, places.name, RGB(255, 255, 255), app.ui.large_font, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    text_utf8(task_prompt(point), places.prompt, RGB(255, 255, 255), places.prompt_font, DT_CENTER | DT_WORDBREAK | DT_END_ELLIPSIS);

    switch (point.task) {
    case task_kind::choice: {
        for (int index = 0; index < point.choice_count; index++) {
            wchar_t label[48];
            utf8_to_wide(point.choices[index], label, 48);
            COLORREF colour = index == app.mission.choice_selected ? RGB(30, 80, 170) : RGB(60, 120, 220);
            if (app.mission.choice_feedback == index) {
                colour = index == point.answer || point.answer < 0 ? RGB(40, 170, 70) : RGB(200, 50, 50);
            }
            rounded_button(places.choices[index], label, colour, app.ui.large_font);
        }
        if (app.mission.choice_feedback >= 0 && app.mission.choice_feedback != point.answer && point.answer >= 0) {
            text(L"Not quite, try again!", places.feedback, RGB(255, 220, 80), app.ui.medium_font, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        break;
    }
    case task_kind::count: {
        rounded_button(places.minus, L"-", RGB(60, 120, 220), app.ui.huge_font);
        rounded_button(places.plus, L"+", RGB(60, 120, 220), app.ui.huge_font);
        wchar_t value[16];
        wsprintfW(value, L"%d", app.mission.count_value);
        text(value, places.value, RGB(255, 255, 255), app.ui.huge_font, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        rounded_button(places.action, L"That's my answer", RGB(40, 170, 70), app.ui.large_font);
        break;
    }
    case task_kind::photo: {
        rounded_button(places.action, L"    Take a photo", RGB(40, 170, 70), app.ui.large_font);
        draw_icon(icon_photo_camera, places.action.left + 30, (places.action.top + places.action.bottom) / 2, RGB(255, 255, 255));
        break;
    }
    default:
        rounded_button(places.action, L"Next!", RGB(40, 170, 70), app.ui.large_font);
        break;
    }
}

bool has_photo_hint() {
    return app.mission.have_mission && app.mission.target >= 0 && app.mission.quest.waypoints[app.mission.target].photo[0];
}

void photo_hint_path(wchar_t *path) {
    lstrcpyW(path, app.mission.missions[app.mission.mission_index].path);
    wchar_t *slash = wcsrchr(path, L'\\');
    if (slash) {
        slash[1] = 0;
    }
    wchar_t name[64];
    utf8_to_wide(app.mission.quest.waypoints[app.mission.target].photo, name, 64);
    lstrcatW(path, name);
}

void open_hint() {
    if (!has_photo_hint()) {
        return;
    }
    app.photos.hint_loaded = false;
    app.photos.view_loaded = false;
    app.ui.current = screen::hint_view;
    app.outing.events.add(app.location.gps_now, "photo hint", app.mission.quest.waypoints[app.mission.target].name, app.mission.quest.waypoints[app.mission.target].photo);
    InvalidateRect(app.ui.window, nullptr, FALSE);
}

void draw_hint_view_screen() {
    if (!create_canvas()) {
        return;
    }
    if (!app.photos.hint_loaded) {
        if (app.photos.view_pixels == nullptr) {
            app.photos.view_pixels = static_cast<uint16_t *>(malloc(camera_width * camera_height * sizeof(uint16_t)));
        }
        wchar_t path[MAX_PATH];
        photo_hint_path(path);
        app.photos.hint_found = app.photos.view_pixels && read_bmp565(path, app.photos.view_pixels, camera_width, camera_height);
        if (!app.photos.hint_found) {
            log_line(L"photo hint %s didn't load (needs a 320x240 16-bit BMP, see tools/make_hint.py)", path);
        }
        app.photos.hint_loaded = true;
    }
    if (app.photos.hint_found) {
        for (int y = 0; y < camera_height; y++) {
            memcpy(app.canvas.bits + y * app.canvas.stride, app.photos.view_pixels + y * camera_width, camera_width * sizeof(uint16_t));
        }
    } else {
        RECT all = {0, 0, camera_width, camera_height};
        fill_canvas(all, RGB(40, 40, 50));
        RECT message = {20, camera_height / 2 - 20, camera_width - 20, camera_height / 2 + 20};
        canvas_text(L"This photo hint is missing.", message, app.ui.medium_font, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    wchar_t name[64];
    utf8_to_wide(app.mission.quest.waypoints[app.mission.target].name, name, 64);
    wchar_t caption[96];
    wsprintfW(caption, L"Photo hint: %s", name);
    RECT caption_area = {8, camera_height - 22, camera_width - 40, camera_height - 2};
    canvas_text(caption, caption_area, app.ui.small_font, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    RECT close = canvas_close_rect();
    fill_canvas(close, RGB(160, 30, 30));
    canvas_text(L"X", close, app.ui.medium_font, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    rotate_canvas_to_screen();
}

void draw_clue_screen() {
    const waypoint &point = app.mission.quest.waypoints[app.mission.target];
    RECT all = {0, 0, app.ui.width, app.ui.height};
    fill(all, RGB(255, 245, 210));
    draw_icon(icon_lightbulb, app.ui.width / 2, layout.bar_height + 26, RGB(230, 160, 0));
    wchar_t heading[64];
    int done = completed_count();
    wsprintfW(heading, done == 0 ? L"Your first stop" : L"Next stop: %d of %d", stop_number(app.mission.quest, app.mission.target), stop_count(app.mission.quest));
    RECT heading_area = {0, layout.bar_height + 44, app.ui.width, layout.bar_height + 64};
    text(heading, heading_area, RGB(160, 100, 0), app.ui.medium_font, DT_CENTER | DT_SINGLELINE);
    RECT name = {10, layout.bar_height + 66, app.ui.width - 10, layout.bar_height + 96};
    text_utf8(point.name, name, RGB(120, 60, 200), app.ui.huge_font, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    bool hint = has_photo_hint();
    int buttons_top = task_button_rect(0, hint ? 2 : 1).top;
    RECT clue = {16, layout.bar_height + 104, app.ui.width - 16, buttons_top - 40};
    text_utf8(point.clue[0] ? point.clue : "Follow the arrow on the map!", clue, RGB(60, 40, 0), app.ui.large_font, DT_CENTER | DT_WORDBREAK);
    if (app.location.gps_now.has_position) {
        wchar_t distance[16];
        format_distance(grid_distance(app.location.position.easting, app.location.position.northing, point.easting, point.northing), distance);
        wchar_t away[48];
        wsprintfW(away, L"%s away", distance);
        RECT away_area = {0, buttons_top - 38, app.ui.width, buttons_top - 4};
        text(away, away_area, RGB(160, 100, 0), app.ui.medium_font, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    if (hint) {
        RECT hint_area = task_button_rect(0, 2);
        rounded_button(hint_area, L"    Photo hint", RGB(120, 60, 200), app.ui.large_font);
        draw_icon(icon_photo_library, hint_area.left + 30, (hint_area.top + hint_area.bottom) / 2, RGB(255, 255, 255));
    }
    rounded_button(task_button_rect(hint ? 1 : 0, hint ? 2 : 1), L"Let's go!", RGB(255, 140, 0), app.ui.large_font);
}

void open_picker() {
    app.mission.picker_selected = app.mission.mission_index >= 0 ? app.mission.mission_index : app.mission.mission_count;
    app.mission.picker_scroll = 0;
    keep_picker_visible();
    app.ui.current = screen::missions;
    redraw();
}

void choose_picker_row(int index) {
    if (index < 0 || index > app.mission.mission_count) {
        return;
    }
    select_mission(index < app.mission.mission_count ? index : -1);
    if (app.mission.have_mission && app.mission.target >= 0) {
        app.map.centre_east = app.mission.quest.waypoints[app.mission.target].easting;
        app.map.centre_north = app.mission.quest.waypoints[app.mission.target].northing;
        show_clue();
    } else {
        show_page(app.mission.have_mission ? screen::list : screen::map);
    }
}

void tap_task(int x, int y) {
    const waypoint &point = app.mission.quest.waypoints[app.mission.task_waypoint];
    task_layout places = layout_task(point);
    POINT tap = {x, y};
    switch (point.task) {
    case task_kind::choice:
        for (int index = 0; index < point.choice_count; index++) {
            if (PtInRect(&places.choices[index], tap)) {
                app.mission.choice_feedback = index;
                app.outing.events.add(app.location.gps_now, "answer", point.name, point.choices[index]);
                if (point.answer < 0 || index == point.answer) {
                    finish_task(point.choices[index]);
                } else {
                    MessageBeep(MB_ICONHAND);
                    redraw();
                }
                return;
            }
        }
        break;
    case task_kind::count: {
        if (PtInRect(&places.minus, tap) && app.mission.count_value > 0) {
            app.mission.count_value--;
            redraw();
        } else if (PtInRect(&places.plus, tap) && app.mission.count_value < max_count_value) {
            app.mission.count_value++;
            redraw();
        } else if (PtInRect(&places.action, tap)) {
            char value[16];
            snprintf(value, sizeof(value), "%d", app.mission.count_value);
            finish_task(value);
        }
        break;
    }
    default: {
        if (PtInRect(&places.action, tap)) {
            if (point.task == task_kind::photo) {
                open_camera(app.mission.task_waypoint);
            } else {
                finish_task("");
            }
        }
        break;
    }
    }
}

void choose_list_row(int index) {
    if (index < 0 || index >= app.mission.quest.count) {
        return;
    }
    set_target(index);
    app.ui.current = screen::map;
    app.map.follow = false;
    app.map.centre_east = app.mission.quest.waypoints[index].easting;
    app.map.centre_north = app.mission.quest.waypoints[index].northing;
    redraw();
}

void tap_list(int x, int y) {
    if (!app.mission.have_mission) {
        tap_nearby(x, y);
        return;
    }
    if (y < list_top()) {
        return;
    }
    choose_list_row(app.mission.list_scroll + (y - list_top()) / layout.list_row_height);
}

void key_task(int key) {
    const waypoint &point = app.mission.quest.waypoints[app.mission.task_waypoint];
    switch (point.task) {
    case task_kind::choice:
        if (key == VK_UP && app.mission.choice_selected > 0) {
            app.mission.choice_selected--;
            redraw();
        } else if (key == VK_DOWN && app.mission.choice_selected < point.choice_count - 1) {
            app.mission.choice_selected++;
            redraw();
        } else if (is_select_key(key)) {
            RECT area = layout_task(point).choices[app.mission.choice_selected];
            tap_task((area.left + area.right) / 2, (area.top + area.bottom) / 2);
        }
        break;
    case task_kind::count:
        if ((key == VK_UP || key == VK_RIGHT) && app.mission.count_value < max_count_value) {
            app.mission.count_value++;
            redraw();
        } else if ((key == VK_DOWN || key == VK_LEFT) && app.mission.count_value > 0) {
            app.mission.count_value--;
            redraw();
        } else if (is_select_key(key)) {
            RECT area = layout_task(point).action;
            tap_task((area.left + area.right) / 2, (area.top + area.bottom) / 2);
        }
        break;
    default:
        if (is_select_key(key)) {
            RECT area = layout_task(point).action;
            tap_task((area.left + area.right) / 2, (area.top + area.bottom) / 2);
        }
        break;
    }
}

}
