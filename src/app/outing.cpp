#include "app.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "icons.h"
#include "log.h"
#include "zip_writer.h"

namespace gismo {

namespace {

constexpr int summary_text_size = 8192;
constexpr int summary_row_height = 24;
constexpr int summary_note_height = 20;

int count_files(const wchar_t *pattern) {
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) {
        return 0;
    }
    int count = 0;
    do {
        count++;
    } while (FindNextFileW(search, &found));
    FindClose(search);
    return count;
}

void wide_to_utf8(const wchar_t *text, char *output, int output_size) {
    if (WideCharToMultiByte(CP_UTF8, 0, text, -1, output, output_size, nullptr, nullptr) == 0) {
        output[0] = 0;
    }
}

void add_folder(zip_writer &archive, const char *base, const wchar_t *folder, const wchar_t *wildcard, const char *subfolder) {
    wchar_t pattern[MAX_PATH];
    wsprintfW(pattern, L"%s\\%s", folder, wildcard);
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        wchar_t path[MAX_PATH];
        wsprintfW(path, L"%s\\%s", folder, found.cFileName);
        char file_name[64];
        wide_to_utf8(found.cFileName, file_name, sizeof(file_name));
        char name[max_zip_name];
        snprintf(name, sizeof(name), "%s/%s%s", base, subfolder, file_name);
        archive.add_file(name, _wfopen(path, L"rb"));
    } while (FindNextFileW(search, &found));
    FindClose(search);
}

void add_session_file(zip_writer &archive, const char *base, const wchar_t *file_name) {
    wchar_t path[MAX_PATH];
    wsprintfW(path, L"%s\\%s", app.outing.session_folder, file_name);
    FILE *file = _wfopen(path, L"rb");
    if (file == nullptr) {
        return;
    }
    char name_text[32];
    wide_to_utf8(file_name, name_text, sizeof(name_text));
    char name[max_zip_name];
    snprintf(name, sizeof(name), "%s/%s", base, name_text);
    archive.add_file(name, file);
}

int append(char *text, int used, const char *format, ...) {
    if (used >= summary_text_size) {
        return used;
    }
    va_list arguments;
    va_start(arguments, format);
    int written = vsnprintf(text + used, summary_text_size - used, format, arguments);
    va_end(arguments);
    return written < 0 ? used : used + written > summary_text_size - 1 ? summary_text_size - 1 : used + written;
}

int write_summary_text(char *text, const outing_stats &stats, const SYSTEMTIME &saved) {
    char title[64];
    snprintf(title, sizeof(title), "%s", app.mission.have_mission ? app.mission.quest.title : "Free roam");
    wchar_t distance[16];
    format_distance(stats.metres, distance);
    wchar_t duration[24];
    format_duration(stats.seconds, duration);
    wchar_t speed[24];
    format_speed(stats, speed);
    char distance_text[16];
    char duration_text[24];
    char speed_text[24];
    wide_to_utf8(distance, distance_text, sizeof(distance_text));
    wide_to_utf8(duration, duration_text, sizeof(duration_text));
    wide_to_utf8(speed, speed_text, sizeof(speed_text));
    const SYSTEMTIME &started = app.outing.started_time;
    int used = 0;
    used = append(text, used, "GISmo outing: %s\n\n", title);
    used = append(text, used, "Started: %04d-%02d-%02d %02d:%02d UTC\n", started.wYear, started.wMonth, started.wDay, started.wHour, started.wMinute);
    used = append(text, used, "Saved: %04d-%02d-%02d %02d:%02d UTC\n", saved.wYear, saved.wMonth, saved.wDay, saved.wHour, saved.wMinute);
    used = append(text, used, "Distance: %s\nTime: %s\nAverage speed: %s\n", distance_text, duration_text, speed_text);
    if (app.mission.have_mission) {
        used = append(text, used, "Stops: %d of %d\n", stats.stops_done, stats.stops_total);
    }
    used = append(text, used, "Photos: %d\n", stats.photos);
    if (app.mission.have_mission) {
        used = append(text, used, "\nStops\n");
        for (int index = 0; index < app.mission.quest.count; index++) {
            const waypoint &point = app.mission.quest.waypoints[index];
            if (point.task == task_kind::via) {
                continue;
            }
            used = append(text, used, "%d. %s: ", stop_number(app.mission.quest, index), point.name);
            if (!point.done) {
                used = append(text, used, "not yet\n");
            } else if (point.result[0]) {
                used = append(text, used, "done %s, %s\n", point.done_time, point.result);
            } else {
                used = append(text, used, "done %s\n", point.done_time);
            }
        }
    }
    return used;
}

}

outing_stats current_outing_stats() {
    outing_stats stats = {};
    stats.metres = app.outing.distance_walked;
    stats.seconds = (GetTickCount() - app.outing.started_tick) / 1000;
    stats.speed_tenths = stats.seconds >= 60 && stats.metres >= 10 ? int(stats.metres * 36 / stats.seconds) : -1;
    stats.stops_total = app.mission.have_mission ? stop_count(app.mission.quest) : 0;
    stats.stops_done = app.mission.have_mission ? completed_count() : 0;
    if (app.outing.session_started) {
        wchar_t pattern[MAX_PATH];
        photo_folder(pattern, L"");
        lstrcatW(pattern, L"\\*.jpg");
        stats.photos = count_files(pattern);
    }
    return stats;
}

void format_duration(DWORD seconds, wchar_t *output) {
    DWORD minutes = seconds / 60;
    if (minutes < 60) {
        wsprintfW(output, L"%u min", minutes);
    } else {
        wsprintfW(output, L"%u h %02u min", minutes / 60, minutes % 60);
    }
}

void format_speed(const outing_stats &stats, wchar_t *output) {
    if (stats.speed_tenths < 0) {
        lstrcpyW(output, L"-");
    } else {
        wsprintfW(output, L"%d.%d km/h", stats.speed_tenths / 10, stats.speed_tenths % 10);
    }
}

bool mission_finished() {
    return app.mission.have_mission && next_target(0) < 0;
}

void open_summary() {
    app.outing.saved_note[0] = 0;
    app.ui.current = screen::summary;
    redraw();
}

bool save_outing() {
    if (!app.outing.session_started) {
        lstrcpyW(app.outing.saved_note, L"Nothing to save yet");
        return false;
    }
    close_run();
    wchar_t folder[MAX_PATH];
    data_root(folder);
    lstrcatW(folder, L"\\Outings");
    CreateDirectoryW(folder, nullptr);
    const wchar_t *stem = app.mission.mission_index >= 0 ? app.mission.missions[app.mission.mission_index].stem : L"free-roam";
    SYSTEMTIME now;
    GetSystemTime(&now);
    wchar_t base_wide[64];
    wsprintfW(base_wide, L"%s-%04d%02d%02d-%02d%02dZ", stem, now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute);
    wchar_t path[MAX_PATH];
    wsprintfW(path, L"%s\\%s.zip", folder, base_wide);
    char base[64];
    wide_to_utf8(base_wide, base, sizeof(base));

    static zip_writer archive;
    static char summary[summary_text_size];
    outing_stats stats = current_outing_stats();
    int summary_length = write_summary_text(summary, stats, now);
    bool ok = archive.open(_wfopen(path, L"wb+"), {now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond});
    if (ok) {
        char name[max_zip_name];
        snprintf(name, sizeof(name), "%s/summary.txt", base);
        archive.add_bytes(name, summary, uint32_t(summary_length));
        if (app.mission.have_mission) {
            snprintf(name, sizeof(name), "%s/mission.txt", base);
            archive.add_file(name, _wfopen(app.mission.missions[app.mission.mission_index].path, L"rb"));
        }
        add_session_file(archive, base, L"progress.txt");
        add_session_file(archive, base, L"events.csv");
        wchar_t tracks[MAX_PATH];
        wsprintfW(tracks, L"%s\\tracks", app.outing.session_folder);
        add_folder(archive, base, tracks, L"*.gpx", "tracks/");
        wchar_t photos[MAX_PATH];
        photo_folder(photos, L"");
        add_folder(archive, base, photos, L"*.jpg", "photos/");
        int entries = archive.entries();
        ok = archive.close();
        log_line(L"outing %s: %s, %d files", path, ok ? L"saved" : L"failed", entries);
    }
    wsprintfW(app.outing.saved_note, ok ? L"Saved %s.zip" : L"Couldn't save the outing", base_wide);
    open_run();
    app.outing.events.add(app.location.gps_now, ok ? "outing saved" : "outing save failed", "", base);
    return ok;
}

void draw_summary_screen() {
    bool finished = mission_finished();
    RECT all = {0, 0, app.ui.width, app.ui.height};
    fill(all, finished ? RGB(40, 170, 70) : RGB(30, 90, 170));
    RECT banner = {0, layout.bar_height + 6, app.ui.width, layout.bar_height + 40};
    text(finished ? L"Mission complete!" : L"Your outing so far", banner, RGB(255, 255, 255), finished ? app.ui.huge_font : app.ui.large_font, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    RECT title = {10, layout.bar_height + 42, app.ui.width - 10, layout.bar_height + 64};
    if (app.mission.have_mission) {
        text_utf8(app.mission.quest.title, title, RGB(255, 240, 160), app.ui.large_font, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    } else {
        text(L"Free roam", title, RGB(255, 240, 160), app.ui.large_font, DT_CENTER | DT_SINGLELINE);
    }

    outing_stats stats = current_outing_stats();
    wchar_t values[5][32];
    const wchar_t *labels[5];
    int rows = 0;
    labels[rows] = L"Distance";
    format_distance(stats.metres, values[rows++]);
    labels[rows] = L"Time";
    format_duration(stats.seconds, values[rows++]);
    labels[rows] = L"Average speed";
    format_speed(stats, values[rows++]);
    if (app.mission.have_mission) {
        labels[rows] = L"Stops found";
        wsprintfW(values[rows++], L"%d of %d", stats.stops_done, stats.stops_total);
    }
    labels[rows] = L"Photos";
    wsprintfW(values[rows++], L"%d", stats.photos);
    RECT save = task_button_rect(0, 2);
    RECT note = {10, save.top - summary_note_height - 2, app.ui.width - 10, save.top - 2};
    int top = title.bottom + 6;
    int row_height = (note.top - top) / rows;
    row_height = row_height > summary_row_height ? summary_row_height : row_height;
    for (int row = 0; row < rows; row++) {
        RECT line = {20, top + row * row_height, app.ui.width - 20, top + (row + 1) * row_height};
        text(labels[row], line, RGB(220, 235, 255), app.ui.medium_font, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        text(values[row], line, RGB(255, 255, 255), app.ui.medium_font, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    if (app.outing.saved_note[0]) {
        text(app.outing.saved_note, note, RGB(255, 240, 160), app.ui.small_font, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    rounded_button(save, L"Save outing", RGB(255, 140, 0), app.ui.large_font);
    rounded_button(task_button_rect(1, 2), L"Back to the map", RGB(20, 50, 100), app.ui.large_font);
}

void tap_summary(int x, int y) {
    POINT tap = {x, y};
    RECT save = task_button_rect(0, 2);
    RECT back = task_button_rect(1, 2);
    if (PtInRect(&save, tap)) {
        save_outing();
        redraw();
    } else if (PtInRect(&back, tap)) {
        app.ui.current = screen::map;
        redraw();
    }
}

}
