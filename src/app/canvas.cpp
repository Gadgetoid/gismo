#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "draw.h"

namespace gismo {

bool create_canvas() {
    if (app.canvas.bits) {
        return true;
    }
    struct {
        BITMAPINFOHEADER header;
        DWORD masks[3];
    } info = {};
    info.header.biSize = sizeof(BITMAPINFOHEADER);
    info.header.biWidth = camera_width;
    info.header.biHeight = -camera_height;
    info.header.biPlanes = 1;
    info.header.biBitCount = 16;
    info.header.biCompression = BI_BITFIELDS;
    info.masks[0] = 0xF800;
    info.masks[1] = 0x07E0;
    info.masks[2] = 0x001F;
    HDC screen_dc = GetDC(app.ui.window);
    app.canvas.dc = CreateCompatibleDC(screen_dc);
    void *bits = nullptr;
    app.canvas.bitmap = CreateDIBSection(screen_dc, reinterpret_cast<BITMAPINFO *>(&info), DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(app.ui.window, screen_dc);
    app.canvas.bits = static_cast<uint16_t *>(bits);
    if (app.canvas.bits == nullptr) {
        return false;
    }
    app.canvas.stride = ((camera_width * 2 + 3) & ~3) / 2;
    SelectObject(app.canvas.dc, app.canvas.bitmap);
    SetBkMode(app.canvas.dc, TRANSPARENT);
    app.camera.frame = static_cast<uint16_t *>(malloc(camera_width * camera_height * sizeof(uint16_t)));
    app.camera.photo_pixels = static_cast<uint16_t *>(malloc(camera_width * camera_height * sizeof(uint16_t)));
    return app.camera.frame && app.camera.photo_pixels;
}

void rotate_canvas_to_screen() {
    for (int y = 0; y < camera_height && y < app.ui.width; y++) {
        const uint16_t *source = app.canvas.bits + y * app.canvas.stride;
        uint16_t *target = app.ui.bits + (app.ui.width - 1 - y);
        for (int x = 0; x < camera_width && x < app.ui.height; x++) {
            target[x * app.ui.stride] = source[x];
        }
    }
}

void screen_to_canvas(int x, int y, int &canvas_x, int &canvas_y) {
    canvas_x = y;
    canvas_y = app.ui.width - 1 - x;
}

void canvas_text(const wchar_t *value, RECT area, HFONT font, UINT format) {
    HGDIOBJ previous = SelectObject(app.canvas.dc, font);
    RECT shadow = {area.left + 1, area.top + 1, area.right + 1, area.bottom + 1};
    SetTextColor(app.canvas.dc, RGB(0, 0, 0));
    DrawTextW(app.canvas.dc, value, -1, &shadow, format);
    SetTextColor(app.canvas.dc, RGB(255, 255, 255));
    DrawTextW(app.canvas.dc, value, -1, &area, format);
    SelectObject(app.canvas.dc, previous);
}

void canvas_circle(int x, int y, int radius, COLORREF outline, COLORREF fill_colour) {
    framebuffer target = {app.canvas.bits, camera_width, camera_height, app.canvas.stride};
    circle_into(target, x, y, radius, outline, 3, fill_colour, true);
}

RECT canvas_close_rect() {
    return {camera_width - 34, 0, camera_width, 30};
}

void fill_canvas(RECT area, COLORREF colour) {
    HBRUSH brush = CreateSolidBrush(colour);
    FillRect(app.canvas.dc, &area, brush);
    DeleteObject(brush);
}

}
