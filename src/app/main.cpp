#include "app.h"

#include <aygshell.h>
#include <pm.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "device.h"
#include "log.h"

namespace gismo {

namespace {

constexpr UINT gps_message = WM_APP + 1;
constexpr int max_logged_keys = 400;

}

application app;

LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    static SHACTIVATEINFO activation = {sizeof(activation)};
    switch (message) {
    case WM_CREATE:
        app.ui.window = window;
        return 0;
    case WM_SIZE:
        create_surface();
        redraw();
        return 0;
    case WM_ACTIVATE:
        SHHandleWMActivate(window, wparam, lparam, &activation, 0);
        if (LOWORD(wparam) != WA_INACTIVE) {
            SHFullScreen(window, SHFS_HIDETASKBAR | SHFS_HIDESIPBUTTON | SHFS_HIDESTARTICON);
            AllKeys(device.capture_all_keys);
        } else {
            AllKeys(FALSE);
        }
        return 0;
    case WM_SETTINGCHANGE:
        SHHandleWMSettingChange(window, wparam, lparam, &activation);
        return 0;
    case WM_PAINT:
        on_paint(window);
        return 0;
    case WM_LBUTTONDOWN:
        on_tap(window, short(LOWORD(lparam)), short(HIWORD(lparam)));
        return 0;
    case WM_MOUSEMOVE:
        if (app.photos.photo_dragging) {
            int y = short(HIWORD(lparam));
            int moved = app.photos.photo_drag_start_y - y;
            if (abs(moved) > 8) {
                app.photos.photo_dragged = true;
            }
            if (app.photos.photo_dragged) {
                int scroll = app.photos.photo_drag_start_scroll + (moved + (moved > 0 ? layout.thumb_row_height / 2 : -layout.thumb_row_height / 2)) / layout.thumb_row_height;
                scroll = scroll < 0 ? 0 : scroll > max_photo_scroll() ? max_photo_scroll() : scroll;
                if (scroll != app.photos.photo_scroll) {
                    app.photos.photo_scroll = scroll;
                    redraw();
                }
            }
            return 0;
        }
        if (app.mission.list_dragging) {
            int y = short(HIWORD(lparam));
            int moved = app.mission.list_drag_start_y - y;
            if (abs(moved) > 8) {
                app.mission.list_dragged = true;
            }
            if (app.mission.list_dragged) {
                int scroll = app.mission.list_drag_start_scroll + (moved + (moved > 0 ? layout.list_row_height / 2 : -layout.list_row_height / 2)) / layout.list_row_height;
                scroll = scroll < 0 ? 0 : scroll > max_list_scroll() ? max_list_scroll() : scroll;
                if (scroll != app.mission.list_scroll) {
                    app.mission.list_scroll = scroll;
                    redraw();
                }
            }
            return 0;
        }
        if (app.input.dragging) {
            POINT smoothed;
            smoothed_drag_point(short(LOWORD(lparam)), short(HIWORD(lparam)), smoothed);
            if (abs(smoothed.x - app.input.drag_start.x) + abs(smoothed.y - app.input.drag_start.y) > 6) {
                app.input.dragged = true;
                app.map.follow = false;
            }
            if (app.input.dragged) {
                double scale = view_metres_per_pixel();
                app.map.centre_east = app.input.drag_east - (smoothed.x - app.input.drag_start.x) * scale;
                app.map.centre_north = app.input.drag_north + (smoothed.y - app.input.drag_start.y) * scale;
                redraw();
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (app.photos.photo_dragging) {
            app.photos.photo_dragging = false;
            ReleaseCapture();
            if (!app.photos.photo_dragged) {
                int x = short(LOWORD(lparam));
                int y = short(HIWORD(lparam));
                int columns = photo_columns();
                int left_margin = (app.ui.width - columns * thumb_width) / 2;
                int column = (x - left_margin) / thumb_width;
                int row = (y - photo_grid_top()) / layout.thumb_row_height + app.photos.photo_scroll;
                if (column >= 0 && column < columns) {
                    open_photo(row * columns + column);
                }
            }
            return 0;
        }
        if (app.mission.list_dragging) {
            app.mission.list_dragging = false;
            ReleaseCapture();
            if (!app.mission.list_dragged) {
                tap_list(short(LOWORD(lparam)), short(HIWORD(lparam)));
            }
            return 0;
        }
        if (app.input.dragging) {
            app.input.dragging = false;
            if (app.settings.log_input && app.input.logged_drags < max_logged_drags) {
                log_line(L"drag end %d,%d", int(short(LOWORD(lparam))), int(short(HIWORD(lparam))));
                app.input.logged_drags++;
            }
            ReleaseCapture();
            int y = short(HIWORD(lparam));
            int strip_top = app.ui.height - layout.button_height - layout.strip_height;
            if (!app.input.dragged && current_target() && y >= strip_top && y < strip_top + layout.strip_height) {
                tap_target_strip();
            }
        }
        return 0;
    case WM_KEYDOWN:
    case WM_KEYUP:
        if (app.settings.log_input && app.input.logged_keys < max_logged_keys) {
            log_line(L"key %s vk %02x lparam %08x", message == WM_KEYDOWN ? L"down" : L"up", unsigned(wparam), unsigned(lparam));
            app.input.logged_keys++;
        }
        if (message == WM_KEYDOWN) {
            if (int(wparam) == device.key_windows) {
                app.input.windows_alone = (lparam & 0x40000000) == 0 ? true : app.input.windows_alone;
            } else {
                app.input.windows_alone = false;
                on_key(int(wparam));
            }
        } else if (int(wparam) == device.key_windows && app.input.windows_alone) {
            app.input.windows_alone = false;
            if (app.ui.current == screen::map || app.ui.current == screen::list || app.ui.current == screen::photos || app.ui.current == screen::sky) {
                show_page(screen::map);
                set_pan_mode(!app.map.pan_mode);
            }
        }
        return 0;
    case WM_TIMER:
        if (wparam == pan_mode_timer) {
            check_pan_mode();
        } else {
            SystemIdleTimerReset();
        }
        return 0;
    case gps_message:
        on_gps_update();
        return 0;
    case camera_message:
        if (app.ui.current == screen::camera) {
            redraw();
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

}

using namespace gismo;

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    wchar_t log_path[MAX_PATH];
    exe_folder(log_path);
    lstrcatW(log_path, L"\\GISmo.log");
    log_open(log_path);
    log_line(L"GISmo starting");
    load_settings();

    HWND existing = FindWindowW(L"GISmo", nullptr);
    if (existing) {
        log_line(L"already running");
        SetForegroundWindow(existing);
        return 0;
    }

    WNDCLASSW window_class = {};
    window_class.lpfnWndProc = window_procedure;
    window_class.hInstance = instance;
    window_class.lpszClassName = L"GISmo";
    RegisterClassW(&window_class);

    app.ui.small_font = make_font(12, false);
    app.ui.medium_font = make_font(15, true);
    app.ui.large_font = make_font(18, true);
    app.ui.huge_font = make_font(26, true);
    app.map.follow = true;
    app.map.zoom = 2;
    app.map.overview_pack = -1;
    app.mission.target = -1;
    app.map.route_target = -1;
    app.map.route_goal_node = -1;
    load_maps();
    load_filters();
    app.camera.filter_index = -1;
    app.camera.camera_task = -1;
    scan_missions();
    app.mission.mission_index = -1;
    int remembered = remembered_mission();
    if (remembered < 0 && app.mission.mission_count == 1) {
        remembered = 0;
    }
    if (remembered >= 0) {
        select_mission(remembered);
        if (app.mission.have_mission && app.mission.target >= 0) {
            app.map.centre_east = app.mission.quest.waypoints[app.mission.target].easting;
            app.map.centre_north = app.mission.quest.waypoints[app.mission.target].northing;
            app.ui.current = screen::clue;
        } else {
            app.ui.current = screen::list;
        }
    } else if (app.mission.mission_count > 1) {
        open_picker();
    }
    app.outing.started_tick = GetTickCount();

    HWND window = CreateWindowW(L"GISmo", L"GISmo", WS_VISIBLE, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), nullptr, nullptr, instance, nullptr);
    if (window == nullptr) {
        log_line(L"CreateWindow failed %u", GetLastError());
        return 1;
    }
    SHFullScreen(window, SHFS_HIDETASKBAR | SHFS_HIDESIPBUTTON | SHFS_HIDESTARTICON);
    MoveWindow(window, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), TRUE);

    app.outing.backlight = SetPowerRequirement(const_cast<wchar_t *>(device.backlight), D0, POWER_NAME, nullptr, 0);
    SetTimer(window, keep_awake_timer, 10000, nullptr);

    wchar_t replay[MAX_PATH];
    exe_folder(replay);
    lstrcatW(replay, L"\\replay.nmea");
    if (GetFileAttributesW(replay) != 0xFFFFFFFF) {
        app.location.gps.start_replay(replay, window, gps_message);
    } else {
        app.location.gps.start(device.gps_port, device.gps_baud, window, gps_message);
    }
    log_line(L"running");

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    AllKeys(FALSE);
    app.camera.device.stop();
    app.location.gps.stop();
    start_session();
    close_run();
    if (app.outing.backlight) {
        ReleasePowerRequirement(app.outing.backlight);
    }
    log_line(L"exit");
    log_close();
    return 0;
}
