#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bitmap_file.h"
#include "device.h"
#include "draw.h"
#include "filter.h"
#include "icons.h"
#include "jpeg.h"
#include "log.h"

namespace gismo {

namespace {

constexpr int filter_wheel_height = 140;
constexpr int camera_gap = 4;
constexpr int camera_bottom_line_height = 22;

constexpr int thumb_factor = 4;
constexpr DWORD flash_ms = 180;
constexpr int jpeg_capacity = 256 * 1024;

}

RECT shutter_rect() {
    return {camera_width - layout.shutter_radius * 2 - 10, camera_height / 2 - layout.shutter_radius, camera_width - 10, camera_height / 2 + layout.shutter_radius};
}

const wchar_t *filter_name() {
    return app.camera.filter_index >= 0 ? app.camera.filters[app.camera.filter_index].name : L"No filter";
}

void apply_filter(const uint16_t *source, uint16_t *target, int target_stride) {
    for (int y = 0; y < camera_height; y++) {
        const uint16_t *row = source + y * camera_width;
        uint16_t *output = target + y * target_stride;
        if (app.camera.filter_index < 0 || app.camera.filter_table == nullptr) {
            memcpy(output, row, camera_width * sizeof(uint16_t));
            continue;
        }
        const uint16_t *table = app.camera.filter_table;
        for (int x = 0; x < camera_width; x++) {
            output[x] = table[row[x]];
        }
    }
}

const wchar_t *filter_name_at(int index) {
    int count = app.camera.filter_count + 1;
    int wrapped = ((index + 1) % count + count) % count - 1;
    return wrapped >= 0 ? app.camera.filters[wrapped].name : L"No filter";
}

const char *camera_prompt() {
    if (app.camera.camera_task >= 0 && app.mission.have_mission) {
        return app.mission.quest.waypoints[app.camera.camera_task].prompt;
    }
    return "";
}

RECT camera_prompt_rect() {
    RECT area = {8, 6, camera_width - 40, camera_height};
    if (camera_prompt()[0] == 0) {
        area.bottom = area.top;
        return area;
    }
    RECT measured = measure_text_utf8(camera_prompt(), area, app.ui.small_font, DT_LEFT | DT_WORDBREAK);
    area.bottom = area.top + (measured.bottom - measured.top);
    RECT wheel = filter_wheel_rect();
    if (area.bottom > wheel.top - camera_gap) {
        area.bottom = wheel.top - camera_gap;
    }
    return area;
}

RECT filter_wheel_rect() {
    int top = camera_height / 2 - filter_wheel_height / 2;
    const char *prompt = camera_prompt();
    if (prompt[0]) {
        RECT area = {8, 6, camera_width - 40, camera_height};
        RECT measured = measure_text_utf8(prompt, area, app.ui.small_font, DT_LEFT | DT_WORDBREAK);
        int below_prompt = area.top + (measured.bottom - measured.top) + camera_gap;
        top = below_prompt > top ? below_prompt : top;
    }
    int lowest = camera_height - camera_bottom_line_height - camera_gap - filter_wheel_height;
    top = top > lowest ? lowest : top;
    return {0, top, 170, top + filter_wheel_height};
}

void draw_filter_wheel() {
    RECT wheel = filter_wheel_rect();
    int middle = (wheel.top + wheel.bottom) / 2;
    draw_icon_into(app.canvas.bits, app.canvas.stride, camera_width, camera_height, icon_expand_less, 20, wheel.top + 12, RGB(255, 255, 255));
    RECT previous = {10, middle - 40, wheel.right, middle - 18};
    canvas_text(filter_name_at(app.camera.filter_index - 1), previous, app.ui.small_font, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    RECT current = {10, middle - 14, wheel.right, middle + 14};
    canvas_text(filter_name(), current, app.ui.large_font, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    RECT next = {10, middle + 18, wheel.right, middle + 40};
    canvas_text(filter_name_at(app.camera.filter_index + 1), next, app.ui.small_font, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    draw_icon_into(app.canvas.bits, app.canvas.stride, camera_width, camera_height, icon_expand_more, 20, wheel.bottom - 12, RGB(255, 255, 255));
}

void draw_camera_screen() {
    if (!create_canvas()) {
        return;
    }
    bool have_frame = app.camera.device.running() && app.camera.device.copy_frame(app.camera.frame);
    if (have_frame) {
        apply_filter(app.camera.frame, app.canvas.bits, app.canvas.stride);
    } else {
        RECT all = {0, 0, camera_width, camera_height};
        fill_canvas(all, RGB(20, 20, 30));
        RECT message = {20, camera_height / 2 - 20, camera_width - 80, camera_height / 2 + 20};
        canvas_text(app.camera.start_failed ? L"The camera isn't working.\nIs another camera app open?" : L"Starting the camera\x2026", message, app.ui.medium_font, DT_CENTER | DT_WORDBREAK);
    }
    if (GetTickCount() - app.camera.flash_tick < flash_ms) {
        for (int y = 0; y < camera_height; y++) {
            uint16_t *row = app.canvas.bits + y * app.canvas.stride;
            for (int x = 0; x < camera_width; x++) {
                row[x] = uint16_t(((row[x] >> 1) & 0x7BEF) + 0x7BEF);
            }
        }
    }

    if (have_frame) {
        draw_filter_wheel();
    }
    if (camera_prompt()[0]) {
        wchar_t wide[160];
        utf8_to_wide(camera_prompt(), wide, 160);
        canvas_text(wide, camera_prompt_rect(), app.ui.small_font, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
    }
    RECT close = canvas_close_rect();
    fill_canvas(close, RGB(160, 30, 30));
    canvas_text(L"X", close, app.ui.medium_font, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    RECT shutter = shutter_rect();
    canvas_circle((shutter.left + shutter.right) / 2, (shutter.top + shutter.bottom) / 2, layout.shutter_radius, RGB(255, 255, 255), RGB(220, 40, 40));

    wchar_t bottom_line[96];
    if (app.camera.camera_note[0]) {
        lstrcpynW(bottom_line, app.camera.camera_note, 96);
    } else {
        wsprintfW(bottom_line, L"Zoom %dx   %d photos", app.camera.device.zoom(), app.camera.photos_taken);
    }
    RECT bottom = {8, camera_height - camera_bottom_line_height, camera_width - 8, camera_height - 2};
    canvas_text(bottom_line, bottom, app.ui.small_font, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

    rotate_canvas_to_screen();
}

void load_filters() {
    wchar_t folder[MAX_PATH];
    find_data_folder(L"Filters", folder);
    wchar_t pattern[MAX_PATH];
    wsprintfW(pattern, L"%s\\*.flt", folder);
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) {
        log_line(L"no filters in %s", folder);
        return;
    }
    do {
        if (app.camera.filter_count >= max_filters || found.cFileName[0] == L'.') {
            continue;
        }
        filter_entry entry;
        wsprintfW(entry.path, L"%s\\%s", folder, found.cFileName);
        lstrcpynW(entry.name, found.cFileName, 48);
        wchar_t *dot = wcsrchr(entry.name, L'.');
        if (dot) {
            *dot = 0;
        }
        int position = app.camera.filter_count;
        while (position > 0 && lstrcmpiW(app.camera.filters[position - 1].name, entry.name) > 0) {
            app.camera.filters[position] = app.camera.filters[position - 1];
            position--;
        }
        app.camera.filters[position] = entry;
        app.camera.filter_count++;
    } while (FindNextFileW(search, &found));
    FindClose(search);
    log_line(L"%d filters", app.camera.filter_count);
}

void select_filter(int index) {
    if (index < -1 || index >= app.camera.filter_count) {
        return;
    }
    app.camera.filter_index = -1;
    if (index < 0) {
        return;
    }
    if (app.camera.filter_table == nullptr) {
        app.camera.filter_table = static_cast<uint16_t *>(malloc(65536 * sizeof(uint16_t)));
        if (app.camera.filter_table == nullptr) {
            return;
        }
    }
    DWORD started = GetTickCount();
    static char text[16384];
    FILE *file = _wfopen(app.camera.filters[index].path, L"rb");
    int length = file ? int(fread(text, 1, sizeof(text), file)) : -1;
    if (file) {
        fclose(file);
    }
    static colour_filter filter;
    if (length <= 0 || !parse_filter(text, length, filter)) {
        log_line(L"filter %s didn't load", app.camera.filters[index].name);
        return;
    }
    build_filter_table(filter, app.camera.filter_table);
    app.camera.filter_index = index;
    log_line(L"filter %s ready in %u ms", app.camera.filters[index].name, GetTickCount() - started);
}

void step_filter(int direction) {
    int next = app.camera.filter_index + direction;
    if (next < -1) {
        next = app.camera.filter_count - 1;
    }
    if (next >= app.camera.filter_count) {
        next = -1;
    }
    select_filter(next);
    app.camera.camera_note[0] = 0;
    redraw();
}

void open_camera(int task) {
    start_session();
    app.camera.camera_task = task;
    app.camera.camera_note[0] = 0;
    app.camera.start_failed = false;
    app.ui.current = screen::camera;
    redraw();
    UpdateWindow(app.ui.window);
    if (!app.camera.device.start(app.ui.window, camera_message)) {
        app.camera.start_failed = true;
        log_line(L"camera didn't start");
    }
    redraw();
}

void close_camera(screen next) {
    app.camera.device.stop();
    app.camera.camera_task = -1;
    app.ui.current = next;
    redraw();
}

void photo_name(wchar_t *name) {
    SYSTEMTIME now;
    GetLocalTime(&now);
    wsprintfW(name, L"photo-%04d%02d%02d-%02d%02d%02d", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    wchar_t path[MAX_PATH];
    photo_folder(path, L"");
    wsprintfW(path + lstrlenW(path), L"\\%s.jpg", name);
    if (GetFileAttributesW(path) != 0xFFFFFFFF) {
        wsprintfW(name + lstrlenW(name), L"-%u", GetTickCount() % 1000);
    }
}

void take_photo() {
    if (!create_canvas() || !app.camera.device.copy_frame(app.camera.frame)) {
        return;
    }
    DWORD started = GetTickCount();
    app.camera.flash_tick = started;
    MessageBeep(MB_OK);
    apply_filter(app.camera.frame, app.camera.photo_pixels, camera_width);

    wchar_t name[40];
    photo_name(name);
    wchar_t folder[MAX_PATH];
    photo_folder(folder, L"");
    CreateDirectoryW(folder, nullptr);
    wchar_t thumbs[MAX_PATH];
    photo_folder(thumbs, L"thumbs");
    CreateDirectoryW(thumbs, nullptr);
    wchar_t views[MAX_PATH];
    photo_folder(views, L"view");
    CreateDirectoryW(views, nullptr);

    if (app.camera.jpeg_buffer == nullptr) {
        app.camera.jpeg_buffer = static_cast<uint8_t *>(malloc(jpeg_capacity));
    }
    photo_tag tag = {};
    tag.has_position = app.location.gps_now.has_position;
    tag.latitude = app.location.gps_now.latitude;
    tag.longitude = app.location.gps_now.longitude;
    tag.altitude = app.location.gps_now.altitude;
    tag.has_time = app.location.gps_now.has_time && app.location.gps_now.has_date;
    tag.time = app.location.gps_now.time;
    int length = app.camera.jpeg_buffer ? encode_jpeg(app.camera.photo_pixels, camera_width, camera_height, int(app.settings.jpeg_quality), &tag, app.camera.jpeg_buffer, jpeg_capacity) : -1;
    DWORD encoded = GetTickCount();

    wchar_t path[MAX_PATH];
    wsprintfW(path, L"%s\\%s.jpg", folder, name);
    bool saved = length > 0 && write_whole_file(path, app.camera.jpeg_buffer, DWORD(length));
    wsprintfW(path, L"%s\\%s.bmp", views, name);
    write_bmp565(path, app.camera.photo_pixels, camera_width, camera_height);
    static uint16_t thumb[thumb_width * thumb_height];
    shrink565(app.camera.photo_pixels, camera_width, camera_height, thumb, thumb_factor);
    wsprintfW(path, L"%s\\%s.bmp", thumbs, name);
    write_bmp565(path, thumb, thumb_width, thumb_height);
    DWORD finished = GetTickCount();

    char file_name[64];
    WideCharToMultiByte(CP_UTF8, 0, name, -1, file_name, sizeof(file_name), nullptr, nullptr);
    char filter_text[64];
    WideCharToMultiByte(CP_UTF8, 0, filter_name(), -1, filter_text, sizeof(filter_text), nullptr, nullptr);
    char detail[128];
    snprintf(detail, sizeof(detail), "%s.jpg, %s", file_name, filter_text);
    const char *waypoint_name = app.camera.camera_task >= 0 && app.mission.have_mission ? app.mission.quest.waypoints[app.camera.camera_task].name : "";
    app.outing.events.add(app.location.gps_now, "photo", waypoint_name, detail);
    app.camera.photos_taken++;
    app.photos.photos_loaded = false;
    log_line(L"photo %s: %d bytes, %s, encode %u ms, save %u ms, %s", name, length, saved ? L"saved" : L"NOT saved", encoded - started, finished - encoded, tag.has_position ? L"geotagged" : L"no position");
    wsprintfW(app.camera.camera_note, saved ? (tag.has_position ? L"Saved %s (geotagged)" : L"Saved %s (no GPS fix)") : L"Couldn't save %s", name);
    redraw();

    if (app.camera.camera_task >= 0 && saved) {
        int task = app.camera.camera_task;
        UpdateWindow(app.ui.window);
        Sleep(400);
        close_camera(screen::task);
        app.mission.task_waypoint = task;
        snprintf(detail, sizeof(detail), "%s.jpg", file_name);
        finish_task(detail);
    }
}

void key_camera_screen(int key) {
    switch (key) {
    case VK_UP:
        app.camera.device.set_zoom(app.camera.device.zoom() + 1);
        app.camera.camera_note[0] = 0;
        redraw();
        break;
    case VK_DOWN:
        app.camera.device.set_zoom(app.camera.device.zoom() - 1);
        app.camera.camera_note[0] = 0;
        redraw();
        break;
    case VK_RIGHT:
        step_filter(-1);
        break;
    case VK_LEFT:
        step_filter(1);
        break;
    default:
        break;
    }
}

void tap_camera_screen(int x, int y) {
    int canvas_x;
    int canvas_y;
    screen_to_canvas(x, y, canvas_x, canvas_y);
    POINT tap = {canvas_x, canvas_y};
    RECT close = canvas_close_rect();
    RECT shutter = shutter_rect();
    if (PtInRect(&close, tap)) {
        close_camera(app.camera.camera_task >= 0 ? screen::task : screen::map);
    } else if (PtInRect(&shutter, tap)) {
        take_photo();
    } else {
        RECT wheel = filter_wheel_rect();
        if (PtInRect(&wheel, tap)) {
            step_filter(canvas_y < (wheel.top + wheel.bottom) / 2 ? -1 : 1);
        }
    }
}

}
