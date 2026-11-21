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

constexpr int max_cached_thumbs = 48;

}

void photo_folder(wchar_t *folder, const wchar_t *subfolder) {
    if (subfolder[0]) {
        wsprintfW(folder, L"%s\\photos\\%s", app.outing.session_folder, subfolder);
    } else {
        wsprintfW(folder, L"%s\\photos", app.outing.session_folder);
    }
}

void load_photos() {
    app.photos.photo_count = 0;
    app.photos.photos_loaded = true;
    if (!app.outing.session_started) {
        return;
    }
    wchar_t pattern[MAX_PATH];
    photo_folder(pattern, L"thumbs");
    lstrcatW(pattern, L"\\*.bmp");
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        if (app.photos.photo_count >= max_photos) {
            break;
        }
        wchar_t name[40];
        lstrcpynW(name, found.cFileName, 40);
        wchar_t *dot = wcsrchr(name, L'.');
        if (dot) {
            *dot = 0;
        }
        int position = app.photos.photo_count;
        while (position > 0 && lstrcmpW(app.photos.photo_names[position - 1], name) < 0) {
            lstrcpyW(app.photos.photo_names[position], app.photos.photo_names[position - 1]);
            position--;
        }
        lstrcpyW(app.photos.photo_names[position], name);
        app.photos.photo_count++;
    } while (FindNextFileW(search, &found));
    FindClose(search);

    if (app.photos.thumbs == nullptr) {
        app.photos.thumbs = static_cast<uint16_t *>(malloc(max_cached_thumbs * thumb_width * thumb_height * sizeof(uint16_t)));
    }
    for (int index = 0; index < app.photos.photo_count && index < max_cached_thumbs && app.photos.thumbs; index++) {
        wchar_t path[MAX_PATH];
        photo_folder(path, L"thumbs");
        wsprintfW(path + lstrlenW(path), L"\\%s.bmp", app.photos.photo_names[index]);
        uint16_t *thumb = app.photos.thumbs + index * thumb_width * thumb_height;
        if (!read_bmp565(path, thumb, thumb_width, thumb_height)) {
            memset(thumb, 0x84, thumb_width * thumb_height * sizeof(uint16_t));
        }
    }
    log_line(L"%d photos", app.photos.photo_count);
}

int photo_grid_top() {
    return layout.bar_height + 28;
}

int photo_columns() {
    return app.ui.width / thumb_width;
}

int visible_photo_rows() {
    return (app.ui.height - layout.button_height - photo_grid_top()) / layout.thumb_row_height;
}

int max_photo_scroll() {
    int rows = (app.photos.photo_count + photo_columns() - 1) / photo_columns();
    int limit = rows - visible_photo_rows();
    return limit > 0 ? limit : 0;
}

RECT photo_camera_rect() {
    int centre_x = app.ui.width - layout.floating_radius - 8;
    int centre_y = app.ui.height - layout.button_height - layout.floating_radius - 8;
    return floating_rect(centre_x, centre_y);
}

void draw_photos_screen() {
    if (!app.photos.photos_loaded) {
        load_photos();
    }
    RECT all = {0, 0, app.ui.width, app.ui.height};
    fill(all, RGB(30, 30, 40));
    draw_mission_header(L"Free roam");
    if (app.photos.photo_count == 0) {
        draw_icon(icon_photo_library, app.ui.width / 2, app.ui.height / 2 - 30, RGB(150, 150, 170));
        RECT message = {16, app.ui.height / 2, app.ui.width - 16, app.ui.height - layout.button_height - 10};
        text(L"No photos yet.\nPress the camera button to take one!", message, RGB(200, 200, 210), app.ui.medium_font, DT_CENTER | DT_WORDBREAK);
    } else {
        int columns = photo_columns();
        int left_margin = (app.ui.width - columns * thumb_width) / 2;
        int bottom = app.ui.height - layout.button_height;
        for (int index = app.photos.photo_scroll * columns; index < app.photos.photo_count; index++) {
            int row = index / columns - app.photos.photo_scroll;
            int top = photo_grid_top() + row * layout.thumb_row_height;
            if (top + thumb_height > bottom) {
                break;
            }
            int left = left_margin + (index % columns) * thumb_width;
            if (index < max_cached_thumbs && app.photos.thumbs) {
                const uint16_t *thumb = app.photos.thumbs + index * thumb_width * thumb_height;
                for (int y = 0; y < thumb_height; y++) {
                    memcpy(app.ui.bits + (top + y) * app.ui.stride + left + 1, thumb + y * thumb_width + 1, (thumb_width - 2) * sizeof(uint16_t));
                }
            } else {
                RECT placeholder = {left + 1, top, left + thumb_width - 1, top + thumb_height};
                fill(placeholder, RGB(70, 70, 80));
            }
        }
    }
    RECT camera_area = photo_camera_rect();
    circle((camera_area.left + camera_area.right) / 2, (camera_area.top + camera_area.bottom) / 2, layout.floating_radius, RGB(255, 255, 255), 2, RGB(200, 40, 40), true);
    draw_icon(icon_photo_camera, (camera_area.left + camera_area.right) / 2, (camera_area.top + camera_area.bottom) / 2, RGB(255, 255, 255));
}

void draw_photo_view_screen() {
    if (!create_canvas()) {
        return;
    }
    if (!app.photos.view_loaded) {
        if (app.photos.view_pixels == nullptr) {
            app.photos.view_pixels = static_cast<uint16_t *>(malloc(camera_width * camera_height * sizeof(uint16_t)));
        }
        wchar_t path[MAX_PATH];
        photo_folder(path, L"view");
        wsprintfW(path + lstrlenW(path), L"\\%s.bmp", app.photos.photo_names[app.photos.viewing]);
        if (app.photos.view_pixels && !read_bmp565(path, app.photos.view_pixels, camera_width, camera_height)) {
            memset(app.photos.view_pixels, 0, camera_width * camera_height * sizeof(uint16_t));
        }
        app.photos.view_loaded = true;
    }
    for (int y = 0; y < camera_height && app.photos.view_pixels; y++) {
        memcpy(app.canvas.bits + y * app.canvas.stride, app.photos.view_pixels + y * camera_width, camera_width * sizeof(uint16_t));
    }
    wchar_t caption[96];
    wsprintfW(caption, L"%d of %d   %s", app.photos.viewing + 1, app.photos.photo_count, app.photos.photo_names[app.photos.viewing]);
    RECT caption_area = {8, camera_height - 22, camera_width - 8, camera_height - 2};
    canvas_text(caption, caption_area, app.ui.small_font, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    RECT close = canvas_close_rect();
    fill_canvas(close, RGB(160, 30, 30));
    canvas_text(L"X", close, app.ui.medium_font, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    rotate_canvas_to_screen();
}

void open_photo(int index) {
    if (index < 0 || index >= app.photos.photo_count) {
        return;
    }
    app.photos.viewing = index;
    app.photos.view_loaded = false;
    app.ui.current = screen::photo_view;
    redraw();
}

void tap_photo_view(int x, int y) {
    int canvas_x;
    int canvas_y;
    screen_to_canvas(x, y, canvas_x, canvas_y);
    POINT tap = {canvas_x, canvas_y};
    RECT close = canvas_close_rect();
    if (PtInRect(&close, tap)) {
        show_page(screen::photos);
    } else if (canvas_x < camera_width / 2) {
        open_photo(app.photos.viewing > 0 ? app.photos.viewing - 1 : app.photos.photo_count - 1);
    } else {
        open_photo(app.photos.viewing + 1 < app.photos.photo_count ? app.photos.viewing + 1 : 0);
    }
}

}
