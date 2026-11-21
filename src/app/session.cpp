#include "app.h"

#include <pm.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

namespace gismo {

void sessions_folder(wchar_t *folder) {
    data_root(folder);
    lstrcatW(folder, L"\\Sessions");
}

void mission_folder(const wchar_t *stem, wchar_t *folder) {
    sessions_folder(folder);
    lstrcatW(folder, L"\\");
    lstrcatW(folder, stem[0] ? stem : L"Free roam");
}

void apply_saved_progress(const wchar_t *stem, mission &target) {
    wchar_t path[MAX_PATH];
    mission_folder(stem, path);
    lstrcatW(path, L"\\progress.txt");
    static char text[16384];
    int length = read_file(path, text, sizeof(text));
    if (length > 0) {
        apply_progress(text, length, target);
    }
}

void scan_missions() {
    wchar_t folder[MAX_PATH];
    find_data_folder(L"Missions", folder);
    wchar_t pattern[MAX_PATH];
    wsprintfW(pattern, L"%s\\*.txt", folder);
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW(pattern, &found);
    app.mission.mission_count = 0;
    if (search == INVALID_HANDLE_VALUE) {
        log_line(L"no missions in %s", folder);
        return;
    }
    static mission scratch;
    static char text[32768];
    do {
        if (app.mission.mission_count >= max_missions) {
            break;
        }
        mission_entry &entry = app.mission.missions[app.mission.mission_count];
        wsprintfW(entry.path, L"%s\\%s", folder, found.cFileName);
        lstrcpynW(entry.stem, found.cFileName, 64);
        wchar_t *dot = wcsrchr(entry.stem, L'.');
        if (dot) {
            *dot = 0;
        }
        char error[160];
        int length = read_file(entry.path, text, sizeof(text));
        entry.valid = length > 0 && parse_mission(text, length, scratch, error, sizeof(error));
        if (entry.valid) {
            apply_saved_progress(entry.stem, scratch);
            snprintf(entry.title, sizeof(entry.title), "%s", scratch.title[0] ? scratch.title : "Mission");
            entry.points = scratch.count;
            for (int point = 0; point < scratch.count; point++) {
                entry.east[point] = scratch.waypoints[point].easting;
                entry.north[point] = scratch.waypoints[point].northing;
                entry.point_done[point] = scratch.waypoints[point].done;
            }
            entry.count = scratch.count;
            entry.done = completed_waypoints(scratch);
        } else {
            WideCharToMultiByte(CP_UTF8, 0, entry.stem, -1, entry.title, sizeof(entry.title), nullptr, nullptr);
            entry.count = 0;
            entry.done = 0;
            entry.points = 0;
        }
        app.mission.mission_count++;
    } while (FindNextFileW(search, &found));
    FindClose(search);
    log_line(L"%d missions", app.mission.mission_count);
}

void remember_mission() {
    wchar_t path[MAX_PATH];
    sessions_folder(path);
    CreateDirectoryW(path, nullptr);
    lstrcatW(path, L"\\last-mission.txt");
    FILE *file = _wfopen(path, L"wb");
    if (file == nullptr) {
        return;
    }
    char stem[128] = "";
    if (app.mission.mission_index >= 0) {
        WideCharToMultiByte(CP_UTF8, 0, app.mission.missions[app.mission.mission_index].stem, -1, stem, sizeof(stem), nullptr, nullptr);
    }
    fputs(stem, file);
    fclose(file);
}

int remembered_mission() {
    wchar_t path[MAX_PATH];
    sessions_folder(path);
    lstrcatW(path, L"\\last-mission.txt");
    char text[128];
    int length = read_file(path, text, sizeof(text) - 1);
    if (length <= 0) {
        return -1;
    }
    text[length] = 0;
    wchar_t stem[64];
    utf8_to_wide(text, stem, 64);
    for (int index = 0; index < app.mission.mission_count; index++) {
        if (lstrcmpiW(app.mission.missions[index].stem, stem) == 0) {
            return index;
        }
    }
    return -1;
}

void save_progress() {
    if (app.mission.mission_index < 0 || !app.mission.have_mission) {
        return;
    }
    wchar_t path[MAX_PATH];
    mission_folder(app.mission.missions[app.mission.mission_index].stem, path);
    CreateDirectoryW(path, nullptr);
    lstrcatW(path, L"\\progress.txt");
    FILE *file = _wfopen(path, L"wb");
    if (file == nullptr) {
        log_line(L"can't write %s", path);
        return;
    }
    write_progress(file, app.mission.quest);
    fclose(file);
    mission_entry &entry = app.mission.missions[app.mission.mission_index];
    entry.done = completed_waypoints(app.mission.quest);
    for (int point = 0; point < entry.points && point < app.mission.quest.count; point++) {
        entry.point_done[point] = app.mission.quest.waypoints[point].done;
    }
}

void close_run() {
    if (!app.outing.run_open) {
        return;
    }
    app.outing.events.add(app.location.gps_now, "stop", "", "");
    app.outing.track.close();
    app.outing.events.close();
    app.outing.run_open = false;
}

void open_run() {
    close_run();
    const wchar_t *stem = app.mission.mission_index >= 0 ? app.mission.missions[app.mission.mission_index].stem : L"";
    wchar_t base[MAX_PATH];
    sessions_folder(base);
    CreateDirectoryW(base, nullptr);
    mission_folder(stem, app.outing.session_folder);
    CreateDirectoryW(app.outing.session_folder, nullptr);

    wchar_t path[MAX_PATH];
    wsprintfW(path, L"%s\\tracks", app.outing.session_folder);
    CreateDirectoryW(path, nullptr);
    wsprintfW(path, L"%s\\photos", app.outing.session_folder);
    CreateDirectoryW(path, nullptr);

    SYSTEMTIME now;
    GetSystemTime(&now);
    wsprintfW(path, L"%s\\tracks\\%04d%02d%02d-%02d%02d%02dZ.gpx", app.outing.session_folder, now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    const char *title = app.mission.have_mission ? app.mission.quest.title : "GISmo";
    app.outing.track.open(_wfopen(path, L"wb+"), title);
    wsprintfW(path, L"%s\\events.csv", app.outing.session_folder);
    app.outing.events.open(_wfopen(path, L"ab"));
    app.outing.events.add(app.location.gps_now, "start", "", title);
    app.outing.run_open = true;
    log_line(L"run %s", path);
}

void start_session() {
    if (app.outing.session_started) {
        return;
    }
    app.outing.session_started = true;
    GetSystemTime(&app.outing.started_time);
    open_run();
}

void set_clock_from_gps() {
    const gps_state &gps = app.location.gps_now;
    if (app.outing.clock_set || !gps.has_date || !gps.has_time || gps.time.year < 2020) {
        return;
    }
    app.outing.clock_set = true;
    SYSTEMTIME current;
    GetSystemTime(&current);
    SYSTEMTIME satellite = {};
    satellite.wYear = WORD(gps.time.year);
    satellite.wMonth = WORD(gps.time.month);
    satellite.wDay = WORD(gps.time.day);
    satellite.wHour = WORD(gps.time.hour);
    satellite.wMinute = WORD(gps.time.minute);
    satellite.wSecond = WORD(gps.time.second);
    FILETIME current_file;
    FILETIME satellite_file;
    SystemTimeToFileTime(&current, &current_file);
    SystemTimeToFileTime(&satellite, &satellite_file);
    ULARGE_INTEGER current_value;
    ULARGE_INTEGER satellite_value;
    current_value.LowPart = current_file.dwLowDateTime;
    current_value.HighPart = current_file.dwHighDateTime;
    satellite_value.LowPart = satellite_file.dwLowDateTime;
    satellite_value.HighPart = satellite_file.dwHighDateTime;
    uint64_t difference = current_value.QuadPart > satellite_value.QuadPart ? current_value.QuadPart - satellite_value.QuadPart : satellite_value.QuadPart - current_value.QuadPart;
    if (difference > 60ull * 10000000ull) {
        SetSystemTime(&satellite);
        log_line(L"clock set from GPS to %04d-%02d-%02d %02d:%02d:%02d UTC", satellite.wYear, satellite.wMonth, satellite.wDay, satellite.wHour, satellite.wMinute, satellite.wSecond);
    }
}

int next_target(int from) {
    if (!app.mission.have_mission) {
        return -1;
    }
    for (int offset = 0; offset < app.mission.quest.count; offset++) {
        int index = (from + offset) % app.mission.quest.count;
        if (!app.mission.quest.waypoints[index].done) {
            return index;
        }
    }
    return -1;
}

int completed_count() {
    return completed_stops(app.mission.quest);
}

void select_mission(int index) {
    static char text[32768];
    app.mission.mission_index = index;
    app.mission.have_mission = false;
    app.mission.mission_error[0] = 0;
    app.mission.target = -1;
    app.mission.list_scroll = 0;
    app.mission.list_selected = 0;
    clear_explore_target();
    app.explore.nearby_valid = false;
    if (index >= 0) {
        mission_entry &entry = app.mission.missions[index];
        int length = read_file(entry.path, text, sizeof(text));
        char error[160] = "can't read the file";
        app.mission.have_mission = length > 0 && parse_mission(text, length, app.mission.quest, error, sizeof(error));
        if (app.mission.have_mission) {
            apply_saved_progress(entry.stem, app.mission.quest);
            app.mission.target = next_target(0);
            app.mission.list_selected = app.mission.target >= 0 ? app.mission.target : 0;
            log_line(L"mission %s: %d waypoints, %d done", entry.stem, app.mission.quest.count, completed_count());
        } else {
            wchar_t wide_error[160];
            utf8_to_wide(error, wide_error, 160);
            wsprintfW(app.mission.mission_error, L"%s: %s", entry.stem, wide_error);
            log_line(L"mission error %s", app.mission.mission_error);
        }
    }
    remember_mission();
    if (app.outing.session_started) {
        open_run();
        app.outing.events.add(app.location.gps_now, "mission", "", app.mission.have_mission ? app.mission.quest.title : "free roam");
    }
}

void update_warmth(double distance) {
    DWORD now = GetTickCount();
    if (app.mission.warmth_tick == 0 || app.mission.warmth_distance <= 0) {
        app.mission.warmth_distance = distance;
        app.mission.warmth_tick = now;
        return;
    }
    if (now - app.mission.warmth_tick < milliseconds(app.settings.warmth_interval_seconds)) {
        return;
    }
    double change = distance - app.mission.warmth_distance;
    app.mission.warmth = change < -app.settings.warmth_threshold_metres ? 1 : change > app.settings.warmth_threshold_metres ? -1 : 0;
    app.mission.warmth_distance = distance;
    app.mission.warmth_tick = now;
}

void open_task(int index) {
    app.mission.task_waypoint = index;
    app.mission.count_value = 0;
    app.mission.choice_feedback = -1;
    app.mission.choice_selected = 0;
    app.ui.current = screen::task;
    app.outing.events.add(app.location.gps_now, "arrived", app.mission.quest.waypoints[index].name, task_name(app.mission.quest.waypoints[index].task));
    MessageBeep(MB_OK);
    redraw();
}

void show_clue() {
    if (app.mission.have_mission && app.mission.target >= 0) {
        app.ui.current = screen::clue;
    } else {
        app.ui.current = screen::map;
    }
    redraw();
}

void finish_task(const char *detail) {
    waypoint &point = app.mission.quest.waypoints[app.mission.task_waypoint];
    mark_done(point, detail);
    app.outing.events.add(app.location.gps_now, "completed", point.name, detail);
    advance_target(app.mission.task_waypoint);
}

void mark_done(waypoint &point, const char *detail) {
    point.done = true;
    snprintf(point.result, sizeof(point.result), "%s", detail);
    if (app.location.gps_now.has_time && app.location.gps_now.has_date) {
        format_utc(app.location.gps_now.time, point.done_time, sizeof(point.done_time));
    } else {
        snprintf(point.done_time, sizeof(point.done_time), "yes");
    }
    save_progress();
}

void advance_target(int from) {
    app.mission.target = next_target(from + 1);
    app.mission.warmth = 0;
    app.mission.warmth_tick = 0;
    if (app.mission.target < 0) {
        app.outing.events.add(app.location.gps_now, "mission complete", "", app.mission.quest.title);
        app.ui.current = screen::summary;
        app.outing.saved_note[0] = 0;
        save_outing();
        redraw();
        return;
    }
    app.map.follow = true;
    if (app.mission.quest.waypoints[app.mission.target].task == task_kind::via) {
        app.ui.current = screen::map;
        redraw();
    } else {
        show_clue();
    }
}

void pass_via(int index) {
    waypoint &point = app.mission.quest.waypoints[index];
    mark_done(point, "");
    app.outing.events.add(app.location.gps_now, "passed", point.name, "");
    MessageBeep(MB_OK);
    advance_target(index);
}

void record_position() {
    const gps_state &gps = app.location.gps_now;
    if (!gps.has_position) {
        return;
    }
    DWORD now = GetTickCount();
    const grid_point *last = app.map.trail_count ? &app.map.trail[app.map.trail_count - 1] : nullptr;
    double moved = last ? grid_distance(last->east, last->north, app.location.position.easting, app.location.position.northing) : 0;
    bool far_enough = last == nullptr || moved_far_enough(moved, app.settings.track_spacing_metres, gps);
    if (!far_enough && now - app.outing.last_track_tick < milliseconds(app.settings.track_interval_seconds)) {
        return;
    }
    app.outing.track.add(gps);
    app.outing.last_track_tick = now;
    if (!far_enough) {
        return;
    }
    app.outing.distance_walked += moved;
    if (app.map.trail_count == max_trail) {
        memmove(app.map.trail, app.map.trail + 1, sizeof(grid_point) * (max_trail - 1));
        app.map.trail_count--;
    }
    app.map.trail[app.map.trail_count++] = {app.location.position.easting, app.location.position.northing};
}

void check_arrival() {
    const waypoint *point = current_target();
    bool browsing = app.ui.current == screen::map || app.ui.current == screen::list || app.ui.current == screen::sky || app.ui.current == screen::photos;
    if (point == nullptr || !browsing || !app.location.gps_now.has_position) {
        return;
    }
    if (grid_distance(app.location.position.easting, app.location.position.northing, point->easting, point->northing) > point->radius) {
        return;
    }
    if (point == &app.explore.spot) {
        open_found();
    } else if (point->task == task_kind::via) {
        pass_via(app.mission.target);
    } else {
        open_task(app.mission.target);
    }
}

void on_gps_update() {
    bool had_position = app.location.gps_now.has_position;
    app.location.gps_now = app.location.gps.snapshot();
    app.location.gps_connected = app.location.gps.connected();
    set_clock_from_gps();
    if (!app.outing.session_started && (app.outing.clock_set || GetTickCount() - app.outing.started_tick > milliseconds(app.settings.clock_wait_seconds))) {
        start_session();
    }
    if (app.location.gps_now.has_position) {
        app.location.position = wgs84_to_british_national_grid(app.location.gps_now.latitude, app.location.gps_now.longitude);
        if (!had_position) {
            app.outing.events.add(app.location.gps_now, "fix", "", "");
        }
        if (app.map.follow && !app.input.dragging) {
            app.map.centre_east = app.location.position.easting;
            app.map.centre_north = app.location.position.northing;
        }
        const waypoint *point = current_target();
        if (point) {
            update_warmth(grid_distance(app.location.position.easting, app.location.position.northing, point->easting, point->northing));
        }
        record_position();
        update_route();
        check_arrival();
    }
    redraw();
}

const waypoint *current_target() {
    if (app.mission.have_mission && app.mission.target >= 0) {
        return &app.mission.quest.waypoints[app.mission.target];
    }
    if (!app.mission.have_mission && app.explore.active) {
        return &app.explore.spot;
    }
    return nullptr;
}

int target_key() {
    if (app.mission.have_mission) {
        return app.mission.target;
    }
    return app.explore.active ? max_waypoints + app.explore.serial : -1;
}

void tap_target_strip() {
    const waypoint *point = current_target();
    if (app.mission.have_mission && point && point->task != task_kind::via) {
        show_clue();
    } else {
        show_page(screen::list);
    }
}

void set_target(int index) {
    app.mission.target = index;
    app.mission.warmth = 0;
    app.mission.warmth_tick = 0;
    app.outing.events.add(app.location.gps_now, "target", app.mission.quest.waypoints[index].name, "");
}

}
