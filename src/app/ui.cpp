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

constexpr int max_cached_radius = 40;

}

void create_surface() {
    if (app.ui.memory_dc) {
        SelectObject(app.ui.memory_dc, app.ui.previous_bitmap);
        DeleteObject(app.ui.bitmap);
        DeleteDC(app.ui.memory_dc);
    }
    RECT client;
    GetClientRect(app.ui.window, &client);
    app.ui.width = client.right;
    app.ui.height = client.bottom;

    struct {
        BITMAPINFOHEADER header;
        DWORD masks[3];
    } info = {};
    info.header.biSize = sizeof(BITMAPINFOHEADER);
    info.header.biWidth = app.ui.width;
    info.header.biHeight = -app.ui.height;
    info.header.biPlanes = 1;
    info.header.biBitCount = 16;
    info.header.biCompression = BI_BITFIELDS;
    info.masks[0] = 0xF800;
    info.masks[1] = 0x07E0;
    info.masks[2] = 0x001F;

    HDC screen_dc = GetDC(app.ui.window);
    app.ui.memory_dc = CreateCompatibleDC(screen_dc);
    void *bits = nullptr;
    app.ui.bitmap = CreateDIBSection(screen_dc, reinterpret_cast<BITMAPINFO *>(&info), DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(app.ui.window, screen_dc);
    app.ui.bits = static_cast<uint16_t *>(bits);
    app.ui.stride = ((app.ui.width * 2 + 3) & ~3) / 2;
    app.ui.previous_bitmap = static_cast<HBITMAP>(SelectObject(app.ui.memory_dc, app.ui.bitmap));
    SetBkMode(app.ui.memory_dc, TRANSPARENT);
    log_line(L"surface %dx%d", app.ui.width, app.ui.height);
}

HFONT make_font(int height, bool bold) {
    LOGFONTW font = {};
    font.lfHeight = -height;
    font.lfWeight = bold ? FW_BOLD : FW_NORMAL;
    font.lfQuality = CLEARTYPE_QUALITY;
    lstrcpyW(font.lfFaceName, L"Tahoma");
    return CreateFontIndirectW(&font);
}

void fill(const RECT &area, COLORREF colour) {
    HBRUSH brush = CreateSolidBrush(colour);
    FillRect(app.ui.memory_dc, &area, brush);
    DeleteObject(brush);
}

void text(const wchar_t *value, RECT area, COLORREF colour, HFONT font, UINT format) {
    if (value[0] == 0) {
        return;
    }
    HGDIOBJ previous = SelectObject(app.ui.memory_dc, font);
    SetTextColor(app.ui.memory_dc, colour);
    DrawTextW(app.ui.memory_dc, value, -1, &area, format);
    SelectObject(app.ui.memory_dc, previous);
}

RECT measure_text(const wchar_t *value, RECT area, HFONT font, UINT format) {
    if (value[0] == 0) {
        area.right = area.left;
        area.bottom = area.top;
        return area;
    }
    HGDIOBJ previous = SelectObject(app.ui.memory_dc, font);
    DrawTextW(app.ui.memory_dc, value, -1, &area, format | DT_CALCRECT);
    SelectObject(app.ui.memory_dc, previous);
    return area;
}

RECT measure_text_utf8(const char *value, RECT area, HFONT font, UINT format) {
    wchar_t wide[512];
    utf8_to_wide(value, wide, 512);
    return measure_text(wide, area, font, format);
}

void text_utf8(const char *value, RECT area, COLORREF colour, HFONT font, UINT format) {
    wchar_t wide[512];
    utf8_to_wide(value, wide, 512);
    text(wide, area, colour, font, format);
}

uint16_t to_565(COLORREF colour) {
    return uint16_t((GetRValue(colour) >> 3) << 11 | (GetGValue(colour) >> 2) << 5 | GetBValue(colour) >> 3);
}

void draw_icon_into(uint16_t *bits, int stride, int width, int height, const icon &glyph, int centre_x, int centre_y, COLORREF colour) {
    int left = centre_x - glyph.size / 2;
    int top = centre_y - glyph.size / 2;
    int red = GetRValue(colour) >> 3;
    int green = GetGValue(colour) >> 2;
    int blue = GetBValue(colour) >> 3;
    for (int y = 0; y < glyph.size; y++) {
        int screen_y = top + y;
        if (screen_y < 0 || screen_y >= height) {
            continue;
        }
        uint16_t *row = bits + screen_y * stride;
        const uint8_t *alpha = glyph.alpha + y * glyph.size;
        for (int x = 0; x < glyph.size; x++) {
            int screen_x = left + x;
            int amount = alpha[x];
            if (amount == 0 || screen_x < 0 || screen_x >= width) {
                continue;
            }
            uint16_t pixel = row[screen_x];
            int pixel_red = pixel >> 11;
            int pixel_green = (pixel >> 5) & 0x3F;
            int pixel_blue = pixel & 0x1F;
            pixel_red += ((red - pixel_red) * amount) / 255;
            pixel_green += ((green - pixel_green) * amount) / 255;
            pixel_blue += ((blue - pixel_blue) * amount) / 255;
            row[screen_x] = uint16_t(pixel_red << 11 | pixel_green << 5 | pixel_blue);
        }
    }
}

void draw_icon(const icon &glyph, int centre_x, int centre_y, COLORREF colour) {
    draw_icon_into(app.ui.bits, app.ui.stride, app.ui.width, app.ui.height, glyph, centre_x, centre_y, colour);
}

const coverage_mask *annulus_mask(int32_t inner, int32_t outer) {
    app.ui.mask_clock++;
    coverage_mask *oldest = &app.ui.masks[0];
    for (coverage_mask &mask : app.ui.masks) {
        if (mask.alpha && mask.inner == inner && mask.outer == outer) {
            mask.last_used = app.ui.mask_clock;
            return &mask;
        }
        if (mask.alpha == nullptr || (oldest->alpha && mask.last_used < oldest->last_used)) {
            oldest = &mask;
        }
    }
    int half = (outer + subpixel / 2 + subpixel - 1) / subpixel;
    int size = half * 2 + 1;
    static uint16_t scratch[(max_cached_radius * 2 + 3) * (max_cached_radius * 2 + 3)];
    memset(scratch, 0, size * size * sizeof(uint16_t));
    framebuffer target = {scratch, size, size, size};
    fill_annulus(target, half * subpixel + subpixel / 2, half * subpixel + subpixel / 2, inner, outer, 0xFFFF);
    free(oldest->alpha);
    oldest->alpha = static_cast<uint8_t *>(malloc(size * size));
    if (oldest->alpha == nullptr) {
        return nullptr;
    }
    for (int index = 0; index < size * size; index++) {
        oldest->alpha[index] = uint8_t(((scratch[index] >> 5) & 0x3F) * 255 / 63);
    }
    oldest->inner = inner;
    oldest->outer = outer;
    oldest->size = size;
    oldest->last_used = app.ui.mask_clock;
    return oldest;
}

void stamp_mask(framebuffer &target, const coverage_mask &mask, int centre_x, int centre_y, uint16_t colour) {
    int half = mask.size / 2;
    for (int y = 0; y < mask.size; y++) {
        int screen_y = centre_y - half + y;
        if (screen_y < 0 || screen_y >= target.height) {
            continue;
        }
        uint16_t *row = target.pixels + screen_y * target.stride;
        const uint8_t *alpha = mask.alpha + y * mask.size;
        for (int x = 0; x < mask.size; x++) {
            int screen_x = centre_x - half + x;
            int amount = alpha[x];
            if (amount == 0 || screen_x < 0 || screen_x >= target.width) {
                continue;
            }
            row[screen_x] = amount == 255 ? colour : blend565(row[screen_x], colour, amount + (amount >> 7));
        }
    }
}

void draw_annulus(framebuffer &target, int x, int y, int32_t inner, int32_t outer, uint16_t colour) {
    if (outer <= max_cached_radius * subpixel) {
        const coverage_mask *mask = annulus_mask(inner, outer);
        if (mask) {
            stamp_mask(target, *mask, x, y, colour);
            return;
        }
    }
    fill_annulus(target, x * subpixel + subpixel / 2, y * subpixel + subpixel / 2, inner, outer, colour);
}

void circle_into(framebuffer &target, int x, int y, int radius, COLORREF outline, int thickness, COLORREF fill_colour, bool filled) {
    int32_t outer = radius * subpixel;
    if (filled) {
        draw_annulus(target, x, y, 0, outer, to_565(fill_colour));
        if (thickness > 0 && outline != fill_colour) {
            draw_annulus(target, x, y, outer - thickness * subpixel, outer, to_565(outline));
        }
    } else {
        draw_annulus(target, x, y, outer - thickness * subpixel / 2, outer + thickness * subpixel / 2, to_565(outline));
    }
}

void circle(int x, int y, int radius, COLORREF outline, int thickness, COLORREF fill_colour, bool filled) {
    framebuffer target = current_framebuffer();
    circle_into(target, x, y, radius, outline, thickness, fill_colour, filled);
}

void rounded_button(const RECT &area, const wchar_t *label, COLORREF background, HFONT font) {
    framebuffer target = current_framebuffer();
    fill_rounded_rectangle(target, area.left, area.top, area.right, area.bottom, 8 * subpixel, to_565(RGB(255, 255, 255)));
    fill_rounded_rectangle(target, area.left + 2, area.top + 2, area.right - 2, area.bottom - 2, 6 * subpixel, to_565(background));
    RECT inner = {area.left + 6, area.top, area.right - 6, area.bottom};
    text(label, inner, RGB(255, 255, 255), font, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void arrow(int centre_x, int centre_y, int size, double degrees, COLORREF colour) {
    double radians = degrees * 3.14159265358979 / 180.0;
    double sine = sin(radians);
    double cosine = cos(radians);
    const double shape[4][2] = {{0, -1.0}, {0.6, 0.8}, {0, 0.4}, {-0.6, 0.8}};
    framebuffer target = current_framebuffer();
    for (int pass = 0; pass < 2; pass++) {
        double scale = pass == 0 ? size + 2.0 : size;
        point_q8 points[4];
        for (int index = 0; index < 4; index++) {
            double x = shape[index][0] * scale;
            double y = shape[index][1] * scale + (pass == 0 ? 0.3 : 0);
            points[index].x = centre_x * subpixel + subpixel / 2 + int32_t((x * cosine - y * sine) * subpixel);
            points[index].y = centre_y * subpixel + subpixel / 2 + int32_t((x * sine + y * cosine) * subpixel);
        }
        fill_polygon(target, points, 4, pass == 0 ? 0xFFFF : to_565(colour));
    }
}

void format_distance(double metres, wchar_t *output) {
    if (metres < 1000) {
        wsprintfW(output, L"%d m", int(metres + 0.5));
    } else {
        int tenths = int(metres / 100 + 0.5);
        wsprintfW(output, L"%d.%d km", tenths / 10, tenths % 10);
    }
}

RECT floating_rect(int centre_x, int centre_y) {
    return {centre_x - layout.floating_radius, centre_y - layout.floating_radius, centre_x + layout.floating_radius + 1, centre_y + layout.floating_radius + 1};
}

RECT button_rect(button which) {
    int width = app.ui.width / page_count;
    int top = app.ui.height - layout.button_height;
    int slot;
    switch (which) {
    case button::page_map:
        slot = 0;
        break;
    case button::page_list:
        slot = 1;
        break;
    case button::page_photos:
        slot = 2;
        break;
    case button::page_sky:
        slot = 3;
        break;
    case button::layer:
        return floating_rect(app.ui.width - layout.floating_radius - 8, layout.bar_height + layout.floating_radius + 8);
    case button::follow:
        return floating_rect(app.ui.width - layout.floating_radius - 8, app.ui.height - layout.button_height - layout.strip_height - layout.floating_radius - 8);
    case button::route:
        return floating_rect(app.ui.width - layout.floating_radius - 8, app.ui.height - layout.button_height - layout.strip_height - layout.floating_radius * 3 - 16);
    case button::close:
        return {app.ui.width - layout.bar_height - 4, 0, app.ui.width, layout.bar_height};
    default:
        return {0, 0, 0, 0};
    }
    return {slot * width, top, slot == page_count - 1 ? app.ui.width : (slot + 1) * width, app.ui.height};
}

button hit_button(int x, int y) {
    const button all[] = {button::close, button::follow, button::layer, button::route, button::page_map, button::page_list, button::page_photos, button::page_sky};
    POINT point = {x, y};
    for (button which : all) {
        if ((which == button::follow || which == button::layer || which == button::route) && app.ui.current != screen::map) {
            continue;
        }
        if (which == button::route && current_target() == nullptr) {
            continue;
        }
        RECT area = button_rect(which);
        if (PtInRect(&area, point)) {
            return which;
        }
    }
    return button::none;
}

void draw_toolbar() {
    struct entry {
        button which;
        const icon *glyph;
        screen page;
    };
    const entry entries[] = {
        {button::page_map, &icon_map, screen::map},
        {button::page_list, &icon_checklist, screen::list},
        {button::page_photos, &icon_photo_library, screen::photos},
        {button::page_sky, &icon_satellite_alt, screen::sky},
    };
    for (const entry &item : entries) {
        RECT area = button_rect(item.which);
        bool active = app.ui.current == item.page || (item.page == screen::list && app.ui.current == screen::missions);
        fill(area, active ? RGB(255, 150, 0) : RGB(30, 60, 110));
        RECT edge = {area.right - 1, area.top, area.right, area.bottom};
        fill(edge, RGB(10, 25, 50));
        draw_icon(*item.glyph, (area.left + area.right) / 2, (area.top + area.bottom) / 2, RGB(255, 255, 255));
    }
}

void draw_floating_button(button which, const icon &glyph, bool active) {
    RECT area = button_rect(which);
    int centre_x = (area.left + area.right) / 2;
    int centre_y = (area.top + area.bottom) / 2;
    circle(centre_x, centre_y, layout.floating_radius, RGB(255, 255, 255), 2, active ? RGB(255, 150, 0) : RGB(30, 60, 110), true);
    draw_icon(glyph, centre_x, centre_y, RGB(255, 255, 255));
}

void draw_status_bar() {
    RECT bar = {0, 0, app.ui.width, layout.bar_height};
    fill(bar, RGB(20, 30, 50));
    const gps_state &gps = app.location.gps_now;
    wchar_t status[96];
    if (!app.location.gps_connected) {
        lstrcpyW(status, L"GPS starting\x2026");
    } else if (!gps.has_position) {
        wsprintfW(status, L"Searching: %d satellites", gps.satellite_count);
    } else {
        char reference[24] = "";
        format_grid_reference(app.location.position, 6, reference, sizeof(reference));
        wsprintfW(status, L"%s %d sats  %S", gps.mode == fix_mode::three_d ? L"3D" : L"2D", gps.satellites_used, reference);
    }
    COLORREF dot = !gps.has_position ? RGB(220, 60, 60) : gps.hdop > 3 ? RGB(240, 180, 0) : RGB(60, 220, 90);
    circle(10, layout.bar_height / 2, 5, dot, 1, dot, true);
    RECT area = {20, 0, app.ui.width - 80, layout.bar_height};
    text(status, area, RGB(255, 255, 255), app.ui.small_font, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    if (app.mission.have_mission) {
        wchar_t progress[16];
        wsprintfW(progress, L"%d/%d", completed_count(), stop_count(app.mission.quest));
        RECT progress_area = {app.ui.width - 80, 0, app.ui.width - layout.bar_height - 8, layout.bar_height};
        text(progress, progress_area, RGB(255, 210, 60), app.ui.small_font, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        draw_icon(icon_star, app.ui.width - layout.bar_height - 52, layout.bar_height / 2, RGB(255, 210, 60));
    }
    RECT close = button_rect(button::close);
    fill(close, RGB(160, 30, 30));
    draw_icon(icon_close, (close.left + close.right) / 2, layout.bar_height / 2, RGB(255, 255, 255));
}

void render() {
    switch (app.ui.current) {
    case screen::sky:
        draw_sky_screen();
        break;
    case screen::list:
        draw_list_screen();
        break;
    case screen::photos:
        draw_photos_screen();
        break;
    case screen::missions:
        draw_missions_screen();
        break;
    case screen::clue:
        draw_clue_screen();
        break;
    case screen::task:
        draw_task_screen();
        break;
    case screen::summary:
        draw_summary_screen();
        break;
    case screen::found:
        draw_found_screen();
        break;
    case screen::camera:
        draw_camera_screen();
        return;
    case screen::photo_view:
        draw_photo_view_screen();
        return;
    case screen::hint_view:
        draw_hint_view_screen();
        return;
    default:
        draw_map_screen();
        break;
    }
    draw_status_bar();
    if (app.ui.current != screen::task && app.ui.current != screen::summary && app.ui.current != screen::clue && app.ui.current != screen::found) {
        draw_toolbar();
    }
}

void redraw() {
    InvalidateRect(app.ui.window, nullptr, FALSE);
}

void on_paint(HWND window) {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(window, &paint);
    if (app.ui.bits == nullptr) {
        create_surface();
    }
    static int paints;
    DWORD started = GetTickCount();
    if (app.ui.bits) {
        render();
        BitBlt(dc, 0, 0, app.ui.width, app.ui.height, app.ui.memory_dc, 0, 0, SRCCOPY);
    }
    EndPaint(window, &paint);
    if (paints < 8) {
        uint32_t reads = 0;
        for (int index = 0; index < app.map.pack_count; index++) {
            reads += app.map.packs[index].cache->reads();
        }
        log_line(L"paint %d in %u ms, %u tiles read so far", paints, GetTickCount() - started, reads);
        paints++;
    }
}

}
