#pragma once

#include <stdint.h>

#include "map_pack.h"

namespace gismo {

constexpr int subpixel = 256;

struct point_q8 {
    int32_t x;
    int32_t y;
};

void fill_annulus(framebuffer &target, int32_t centre_x, int32_t centre_y, int32_t inner_radius, int32_t outer_radius, uint16_t colour);

void fill_polygon(framebuffer &target, const point_q8 *points, int count, uint16_t colour);

void draw_line(framebuffer &target, point_q8 from, point_q8 to, int32_t width, uint16_t colour);

void fill_rounded_rectangle(framebuffer &target, int left, int top, int right, int bottom, int32_t radius, uint16_t colour);

uint16_t blend565(uint16_t background, uint16_t foreground, int coverage);

}
