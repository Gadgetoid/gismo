#include "app.h"

#include <math.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace gismo {

void draw_sky_screen() {
    RECT all = {0, 0, app.ui.width, app.ui.height};
    fill(all, RGB(10, 15, 40));
    int top = layout.bar_height;
    int bottom = app.ui.height - layout.button_height;
    int bars_height = 74;
    int available = bottom - top - bars_height - 24;
    int radius = (app.ui.width < available ? app.ui.width : available) / 2 - 10;
    int centre_x = app.ui.width / 2;
    int centre_y = top + 20 + radius;

    RECT title = {0, top + 2, app.ui.width, top + 20};
    text(L"Satellites in the sky", title, RGB(200, 200, 255), app.ui.small_font, DT_CENTER | DT_SINGLELINE);
    for (int elevation = 0; elevation < 90; elevation += 30) {
        circle(centre_x, centre_y, radius * (90 - elevation) / 90, RGB(60, 80, 130), 1, 0, false);
    }
    circle(centre_x, centre_y, 3, RGB(255, 255, 255), 1, RGB(255, 255, 255), true);
    RECT north = {centre_x - 10, centre_y - radius - 2, centre_x + 10, centre_y - radius + 14};
    text(L"N", north, RGB(255, 255, 255), app.ui.small_font, DT_CENTER | DT_SINGLELINE);

    const gps_state &gps = app.location.gps_now;
    int count = gps.satellite_count;
    int bar_width = count ? app.ui.width / count : app.ui.width;
    if (bar_width > 30) {
        bar_width = 30;
    }
    int bars_left = (app.ui.width - bar_width * count) / 2;
    for (int index = 0; index < count; index++) {
        const satellite &entry = gps.satellites[index];
        COLORREF signal = entry.snr == 0 ? RGB(110, 110, 110) : entry.snr < 20 ? RGB(220, 60, 60) : entry.snr < 30 ? RGB(240, 180, 0) : RGB(60, 220, 90);
        double distance = radius * (90 - entry.elevation) / 90.0;
        double angle = entry.azimuth * 3.14159265358979 / 180.0;
        int x = centre_x + int(sin(angle) * distance);
        int y = centre_y - int(cos(angle) * distance);
        circle(x, y, 10, signal, 2, signal, entry.used);
        wchar_t label[8];
        wsprintfW(label, L"%d", entry.prn);
        RECT label_area = {x - 12, y - 8, x + 12, y + 8};
        text(label, label_area, entry.used ? RGB(0, 0, 0) : RGB(255, 255, 255), app.ui.small_font, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        int bar_left = bars_left + index * bar_width + 2;
        int bar_top = bottom - 16 - entry.snr * (bars_height - 20) / 50;
        RECT bar = {bar_left, bar_top, bar_left + bar_width - 4, bottom - 16};
        fill(bar, signal);
        RECT prn = {bar_left - 2, bottom - 16, bar_left + bar_width - 2, bottom};
        text(label, prn, RGB(255, 255, 255), app.ui.small_font, DT_CENTER | DT_SINGLELINE);
    }
    if (count == 0) {
        RECT message = {0, centre_y + radius + 6, app.ui.width, bottom - 20};
        text(L"Looking for satellites\x2026\nGo outside for a better view!", message, RGB(200, 200, 255), app.ui.small_font, DT_CENTER | DT_WORDBREAK);
    }
}

}
