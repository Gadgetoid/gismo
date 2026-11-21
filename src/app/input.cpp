#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "device.h"
#include "log.h"

namespace gismo {

namespace {

constexpr int max_logged_drag_samples = 40;

}

void show_page(screen page) {
    if (page == screen::list && app.ui.current != screen::list) {
        if (app.mission.have_mission) {
            app.mission.list_selected = app.mission.target >= 0 ? app.mission.target : 0;
            keep_selection_visible();
        } else {
            refresh_nearby(false);
        }
    }
    app.ui.current = page;
    redraw();
}

void step_page(int direction) {
    screen current = app.ui.current == screen::missions ? screen::list : app.ui.current == screen::clue ? screen::map : app.ui.current;
    int index = 0;
    for (int candidate = 0; candidate < page_count; candidate++) {
        if (pages[candidate] == current) {
            index = candidate;
        }
    }
    show_page(pages[(index + direction + page_count) % page_count]);
}

void press(button which) {
    switch (which) {
    case button::page_map:
        show_page(screen::map);
        return;
    case button::page_list:
        show_page(screen::list);
        return;
    case button::page_photos:
        show_page(screen::photos);
        return;
    case button::page_sky:
        show_page(screen::sky);
        return;
    case button::follow:
        app.map.follow = true;
        app.ui.current = screen::map;
        if (app.location.gps_now.has_position) {
            app.map.centre_east = app.location.position.easting;
            app.map.centre_north = app.location.position.northing;
        }
        break;
    case button::route:
        app.map.route_hint = !app.map.route_hint;
        if (app.map.route_hint && !load_routes()) {
            app.map.route_hint = false;
        }
        app.map.route_valid = false;
        update_route();
        app.ui.current = screen::map;
        break;
    case button::layer:
        if (app.map.detail_count > 1) {
            app.map.layer = (app.map.layer + 1) % app.map.detail_count;
            log_line(L"layer %s", app.map.packs[app.map.detail_packs[app.map.layer]].name);
        }
        app.ui.current = screen::map;
        break;
    case button::close:
        DestroyWindow(app.ui.window);
        return;
    default:
        break;
    }
    redraw();
}

void on_tap(HWND window, int x, int y) {
    if (app.ui.current == screen::hint_view) {
        app.ui.current = screen::clue;
        redraw();
        return;
    }
    if (app.ui.current == screen::camera) {
        tap_camera_screen(x, y);
        return;
    }
    if (app.ui.current == screen::photo_view) {
        tap_photo_view(x, y);
        return;
    }
    RECT close = button_rect(button::close);
    POINT tap = {x, y};
    if (PtInRect(&close, tap)) {
        press(button::close);
        return;
    }
    if (app.ui.current == screen::task) {
        tap_task(x, y);
        return;
    }
    if (app.ui.current == screen::clue && has_photo_hint()) {
        RECT hint_area = task_button_rect(0, 2);
        RECT go = task_button_rect(1, 2);
        if (PtInRect(&hint_area, tap)) {
            open_hint();
        } else if (PtInRect(&go, tap)) {
            app.ui.current = screen::map;
            redraw();
        }
        return;
    }
    if (app.ui.current == screen::found) {
        show_page(screen::list);
        return;
    }
    if (app.ui.current == screen::summary) {
        tap_summary(x, y);
        return;
    }
    if (app.ui.current == screen::clue) {
        RECT done = task_button_rect(0, 1);
        if (PtInRect(&done, tap)) {
            app.ui.current = screen::map;
            redraw();
        }
        return;
    }
    button which = hit_button(x, y);
    if (which != button::none) {
        press(which);
        return;
    }
    RECT header = mission_header_rect();
    if ((app.ui.current == screen::list || app.ui.current == screen::photos) && PtInRect(&header, tap)) {
        RECT summary = header_summary_rect();
        if (PtInRect(&summary, tap)) {
            open_summary();
        } else {
            open_picker();
        }
        return;
    }
    if (app.ui.current == screen::missions) {
        if (y >= picker_top()) {
            int row = app.mission.picker_scroll + (y - picker_top()) / layout.list_row_height;
            if (row == app.mission.picker_selected) {
                choose_picker_row(row);
            } else if (row < picker_rows()) {
                app.mission.picker_selected = row;
                redraw();
            }
        }
        return;
    }
    if (app.ui.current == screen::photos) {
        RECT camera_area = photo_camera_rect();
        if (PtInRect(&camera_area, tap)) {
            open_camera(-1);
            return;
        }
        if (y >= photo_grid_top()) {
            app.photos.photo_dragging = true;
            app.photos.photo_dragged = false;
            app.photos.photo_drag_start_y = y;
            app.photos.photo_drag_start_scroll = app.photos.photo_scroll;
            SetCapture(window);
        }
        return;
    }
    if (app.ui.current == screen::list && (app.mission.have_mission || !app.mission.mission_error[0])) {
        app.mission.list_dragging = true;
        app.mission.list_dragged = false;
        app.mission.list_drag_start_y = y;
        app.mission.list_drag_start_scroll = app.mission.list_scroll;
        SetCapture(window);
        return;
    }
    if (app.ui.current == screen::map) {
        app.input.dragging = true;
        app.input.dragged = false;
        app.input.drag_start = tap;
        app.input.drag_east = app.map.centre_east;
        app.input.drag_north = app.map.centre_north;
        for (POINT &sample : app.input.drag_samples) {
            sample = tap;
        }
        app.input.drag_sample_index = 0;
        app.input.drag_last_raw = tap;
        app.input.logged_drag_samples = 0;
        if (app.settings.log_input && app.input.logged_drags < max_logged_drags) {
            log_line(L"drag start %d,%d", x, y);
        }
        SetCapture(window);
    }
}

int median_of_three(int a, int b, int c) {
    if (a > b) {
        int swap = a;
        a = b;
        b = swap;
    }
    if (b > c) {
        b = c;
    }
    return a > b ? a : b;
}

bool smoothed_drag_point(int x, int y, POINT &smoothed) {
    bool rejected = abs(x - app.input.drag_last_raw.x) > int(app.settings.max_drag_jump_pixels) || abs(y - app.input.drag_last_raw.y) > int(app.settings.max_drag_jump_pixels);
    if (!rejected) {
        app.input.drag_last_raw = {x, y};
        app.input.drag_samples[app.input.drag_sample_index] = {x, y};
        app.input.drag_sample_index = (app.input.drag_sample_index + 1) % 3;
    }
    smoothed.x = median_of_three(app.input.drag_samples[0].x, app.input.drag_samples[1].x, app.input.drag_samples[2].x);
    smoothed.y = median_of_three(app.input.drag_samples[0].y, app.input.drag_samples[1].y, app.input.drag_samples[2].y);
    if (app.settings.log_input && app.input.logged_drags < max_logged_drags && app.input.logged_drag_samples < max_logged_drag_samples) {
        log_line(L"drag %d,%d -> %d,%d%s", x, y, int(smoothed.x), int(smoothed.y), rejected ? L" rejected" : L"");
        app.input.logged_drag_samples++;
    }
    return !rejected;
}

bool is_select_key(int key) {
    return key == VK_RETURN || key == VK_SPACE || key == device.key_ok || key == device.key_camera;
}

bool on_button_key(int key) {
    if (app.ui.current == screen::hint_view) {
        app.ui.current = screen::clue;
        redraw();
        return true;
    }
    if (app.ui.current == screen::camera) {
        if (key == device.key_previous_page || key == device.key_next_page || key == device.key_home || key == device.key_mail) {
            close_camera(app.camera.camera_task >= 0 ? screen::task : screen::map);
            return true;
        }
        if (is_select_key(key)) {
            take_photo();
            return true;
        }
        key_camera_screen(key);
        return true;
    }
    if (app.ui.current == screen::photo_view) {
        if (key == VK_RIGHT || key == VK_UP) {
            open_photo(app.photos.viewing > 0 ? app.photos.viewing - 1 : app.photos.photo_count - 1);
        } else if (key == VK_LEFT || key == VK_DOWN) {
            open_photo(app.photos.viewing + 1 < app.photos.photo_count ? app.photos.viewing + 1 : 0);
        } else if (key == device.key_camera) {
            open_camera(-1);
        } else if (key == device.key_previous_page || key == device.key_next_page || is_select_key(key)) {
            show_page(screen::photos);
        }
        return true;
    }
    if (key == device.key_camera) {
        if (app.ui.current == screen::task) {
            if (app.mission.quest.waypoints[app.mission.task_waypoint].task == task_kind::photo) {
                open_camera(app.mission.task_waypoint);
                return true;
            }
            return false;
        }
        if (app.ui.current != screen::summary && app.ui.current != screen::clue) {
            open_camera(-1);
        }
        return true;
    }
    bool on_page = app.ui.current != screen::task && app.ui.current != screen::summary && app.ui.current != screen::clue && app.ui.current != screen::found;
    if (key == device.key_previous_page) {
        if (on_page) {
            step_page(-1);
        }
        return true;
    }
    if (key == device.key_next_page) {
        if (on_page) {
            step_page(1);
        }
        return true;
    }
    if (key == device.key_home) {
        if (on_page) {
            press(button::follow);
        }
        return true;
    }
    if (key == device.key_mail) {
        if (on_page) {
            press(button::layer);
        }
        return true;
    }
    return false;
}

void on_key(int key) {
    if (on_button_key(key)) {
        return;
    }
    switch (app.ui.current) {
    case screen::task:
        key_task(key);
        return;
    case screen::found:
        if (is_select_key(key)) {
            show_page(screen::list);
        }
        return;
    case screen::summary:
    case screen::clue:
        if (is_select_key(key)) {
            app.ui.current = screen::map;
            redraw();
        } else if (app.ui.current == screen::clue && (key == VK_LEFT || key == VK_RIGHT)) {
            open_hint();
        }
        return;
    case screen::missions:
        if (key == VK_UP && app.mission.picker_selected > 0) {
            app.mission.picker_selected--;
            keep_picker_visible();
            redraw();
        } else if (key == VK_DOWN && app.mission.picker_selected < picker_rows() - 1) {
            app.mission.picker_selected++;
            keep_picker_visible();
            redraw();
        } else if (is_select_key(key)) {
            choose_picker_row(app.mission.picker_selected);
        } else if (key == VK_RIGHT) {
            show_page(screen::list);
        }
        return;
    case screen::photos:
        if (key == VK_LEFT) {
            open_picker();
        } else if (key == VK_UP && app.photos.photo_scroll > 0) {
            app.photos.photo_scroll--;
            redraw();
        } else if (key == VK_DOWN && app.photos.photo_scroll < max_photo_scroll()) {
            app.photos.photo_scroll++;
            redraw();
        } else if (is_select_key(key) && app.photos.photo_count > 0) {
            open_photo(app.photos.photo_scroll * photo_columns());
        }
        return;
    case screen::list:
        if (!app.mission.have_mission) {
            if (app.mission.mission_error[0]) {
                if (key == VK_LEFT || is_select_key(key)) {
                    open_picker();
                }
            } else {
                key_nearby(key);
            }
            return;
        }
        if (key == VK_UP && app.mission.list_selected > 0) {
            app.mission.list_selected--;
            keep_selection_visible();
            redraw();
        } else if (key == VK_DOWN && app.mission.list_selected < app.mission.quest.count - 1) {
            app.mission.list_selected++;
            keep_selection_visible();
            redraw();
        } else if (is_select_key(key)) {
            choose_list_row(app.mission.list_selected);
        } else if (key == VK_LEFT) {
            open_picker();
        } else if (key == VK_RIGHT) {
            open_summary();
        }
        return;
    case screen::sky:
        if (key == VK_LEFT || is_select_key(key)) {
            show_page(screen::map);
        }
        return;
    default:
        break;
    }
    switch (key) {
    case VK_UP:
        if (app.map.pan_mode) {
            app.map.pan_mode_tick = GetTickCount();
            pan_by(0, -1);
        } else {
            zoom_by(-1);
        }
        break;
    case VK_DOWN:
        if (app.map.pan_mode) {
            app.map.pan_mode_tick = GetTickCount();
            pan_by(0, 1);
        } else {
            zoom_by(1);
        }
        break;
    case VK_LEFT:
        app.map.pan_mode_tick = GetTickCount();
        pan_by(-1, 0);
        break;
    case VK_RIGHT:
        app.map.pan_mode_tick = GetTickCount();
        pan_by(1, 0);
        break;
    default:
        if (is_select_key(key)) {
            press(button::follow);
        }
        break;
    }
}

}
