#include "draw.h"

namespace gismo {

namespace {

uint32_t square_root(uint64_t value) {
    uint64_t result = 0;
    uint64_t bit = uint64_t(1) << 62;
    while (bit > value) {
        bit >>= 2;
    }
    while (bit) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return uint32_t(result);
}

int clamp_coverage(int32_t value) {
    return value < 0 ? 0 : value > subpixel ? subpixel : int(value);
}

void blend_pixel(framebuffer &target, int x, int y, uint16_t colour, int coverage) {
    if (coverage <= 0 || x < 0 || y < 0 || x >= target.width || y >= target.height) {
        return;
    }
    uint16_t &pixel = target.pixels[y * target.stride + x];
    pixel = coverage >= subpixel ? colour : blend565(pixel, colour, coverage);
}

int32_t floor_pixel(int32_t value) {
    return value >= 0 ? value / subpixel : -((-value + subpixel - 1) / subpixel);
}

int32_t span_half_width(int32_t radius, int32_t row_offset) {
    int64_t remaining = int64_t(radius) * radius - int64_t(row_offset) * row_offset;
    return remaining > 0 ? int32_t(square_root(uint64_t(remaining))) : -1;
}

}

uint16_t blend565(uint16_t background, uint16_t foreground, int coverage) {
    int red = background >> 11;
    int green = (background >> 5) & 0x3F;
    int blue = background & 0x1F;
    red += (((foreground >> 11) - red) * coverage) >> 8;
    green += ((((foreground >> 5) & 0x3F) - green) * coverage) >> 8;
    blue += (((foreground & 0x1F) - blue) * coverage) >> 8;
    return uint16_t(red << 11 | green << 5 | blue);
}

void fill_annulus(framebuffer &target, int32_t centre_x, int32_t centre_y, int32_t inner_radius, int32_t outer_radius, uint16_t colour) {
    int32_t reach = outer_radius + subpixel / 2;
    int32_t hole = inner_radius > subpixel / 2 ? inner_radius - subpixel / 2 : 0;
    int top = floor_pixel(centre_y - reach);
    int bottom = floor_pixel(centre_y + reach);
    if (top < 0) {
        top = 0;
    }
    if (bottom >= target.height) {
        bottom = target.height - 1;
    }
    for (int y = top; y <= bottom; y++) {
        int32_t offset_y = y * subpixel + subpixel / 2 - centre_y;
        int32_t outer_half = span_half_width(reach, offset_y);
        if (outer_half < 0) {
            continue;
        }
        int32_t hole_half = inner_radius > 0 ? span_half_width(hole, offset_y) : -1;
        int left = floor_pixel(centre_x - outer_half);
        int right = floor_pixel(centre_x + outer_half);
        int hole_left = hole_half >= 0 ? floor_pixel(centre_x - hole_half) + 1 : right + 1;
        int hole_right = hole_half >= 0 ? floor_pixel(centre_x + hole_half) - 1 : right;
        if (left < 0) {
            left = 0;
        }
        if (right >= target.width) {
            right = target.width - 1;
        }
        for (int x = left; x <= right; x++) {
            if (x >= hole_left && x <= hole_right) {
                x = hole_right;
                continue;
            }
            int32_t offset_x = x * subpixel + subpixel / 2 - centre_x;
            int32_t distance = int32_t(square_root(uint64_t(int64_t(offset_x) * offset_x + int64_t(offset_y) * offset_y)));
            int coverage = clamp_coverage(outer_radius + subpixel / 2 - distance);
            if (inner_radius > 0) {
                coverage -= clamp_coverage(inner_radius + subpixel / 2 - distance);
            }
            blend_pixel(target, x, y, colour, coverage);
        }
    }
}

void fill_polygon(framebuffer &target, const point_q8 *points, int count, uint16_t colour) {
    if (count < 3) {
        return;
    }
    int32_t minimum_x = points[0].x;
    int32_t maximum_x = points[0].x;
    int32_t minimum_y = points[0].y;
    int32_t maximum_y = points[0].y;
    for (int index = 1; index < count; index++) {
        minimum_x = points[index].x < minimum_x ? points[index].x : minimum_x;
        maximum_x = points[index].x > maximum_x ? points[index].x : maximum_x;
        minimum_y = points[index].y < minimum_y ? points[index].y : minimum_y;
        maximum_y = points[index].y > maximum_y ? points[index].y : maximum_y;
    }
    int left = floor_pixel(minimum_x);
    int right = floor_pixel(maximum_x);
    int top = floor_pixel(minimum_y);
    int bottom = floor_pixel(maximum_y);
    constexpr int samples = 4;
    for (int y = top; y <= bottom; y++) {
        for (int x = left; x <= right; x++) {
            int inside = 0;
            for (int sample_y = 0; sample_y < samples; sample_y++) {
                int32_t py = y * subpixel + (sample_y * 2 + 1) * subpixel / (samples * 2);
                for (int sample_x = 0; sample_x < samples; sample_x++) {
                    int32_t px = x * subpixel + (sample_x * 2 + 1) * subpixel / (samples * 2);
                    bool crossed = false;
                    for (int index = 0, previous = count - 1; index < count; previous = index++) {
                        const point_q8 &a = points[index];
                        const point_q8 &b = points[previous];
                        if ((a.y > py) != (b.y > py)) {
                            int64_t cross_x = a.x + int64_t(b.x - a.x) * (py - a.y) / (b.y - a.y);
                            if (px < cross_x) {
                                crossed = !crossed;
                            }
                        }
                    }
                    inside += crossed ? 1 : 0;
                }
            }
            blend_pixel(target, x, y, colour, inside * subpixel / (samples * samples));
        }
    }
}

void draw_line(framebuffer &target, point_q8 from, point_q8 to, int32_t width, uint16_t colour) {
    int32_t half = width / 2;
    int32_t reach = half + subpixel / 2;
    int left = floor_pixel((from.x < to.x ? from.x : to.x) - reach);
    int right = floor_pixel((from.x > to.x ? from.x : to.x) + reach);
    int top = floor_pixel((from.y < to.y ? from.y : to.y) - reach);
    int bottom = floor_pixel((from.y > to.y ? from.y : to.y) + reach);
    if (left < 0) {
        left = 0;
    }
    if (top < 0) {
        top = 0;
    }
    if (right >= target.width) {
        right = target.width - 1;
    }
    if (bottom >= target.height) {
        bottom = target.height - 1;
    }
    int64_t direction_x = to.x - from.x;
    int64_t direction_y = to.y - from.y;
    int64_t length_squared = direction_x * direction_x + direction_y * direction_y;
    int64_t reach_squared = int64_t(reach) * reach;
    for (int y = top; y <= bottom; y++) {
        int64_t py = y * subpixel + subpixel / 2;
        for (int x = left; x <= right; x++) {
            int64_t px = x * subpixel + subpixel / 2;
            int64_t relative_x = px - from.x;
            int64_t relative_y = py - from.y;
            int64_t nearest_x = from.x;
            int64_t nearest_y = from.y;
            if (length_squared > 0) {
                int64_t along = relative_x * direction_x + relative_y * direction_y;
                if (along >= length_squared) {
                    nearest_x = to.x;
                    nearest_y = to.y;
                } else if (along > 0) {
                    nearest_x = from.x + direction_x * along / length_squared;
                    nearest_y = from.y + direction_y * along / length_squared;
                }
            }
            int64_t offset_x = px - nearest_x;
            int64_t offset_y = py - nearest_y;
            int64_t distance_squared = offset_x * offset_x + offset_y * offset_y;
            if (distance_squared >= reach_squared) {
                continue;
            }
            int32_t distance = int32_t(square_root(uint64_t(distance_squared)));
            blend_pixel(target, x, y, colour, clamp_coverage(half + subpixel / 2 - distance));
        }
    }
}

void fill_rounded_rectangle(framebuffer &target, int left, int top, int right, int bottom, int32_t radius, uint16_t colour) {
    int corner = (radius + subpixel - 1) / subpixel;
    for (int y = top; y < bottom; y++) {
        if (y < 0 || y >= target.height) {
            continue;
        }
        bool corner_row = y < top + corner || y >= bottom - corner;
        for (int x = left; x < right; x++) {
            if (x < 0 || x >= target.width) {
                continue;
            }
            bool corner_column = x < left + corner || x >= right - corner;
            if (!corner_row || !corner_column) {
                target.pixels[y * target.stride + x] = colour;
                continue;
            }
            int32_t corner_x = (x < left + corner ? left * subpixel + radius : right * subpixel - radius);
            int32_t corner_y = (y < top + corner ? top * subpixel + radius : bottom * subpixel - radius);
            int32_t offset_x = x * subpixel + subpixel / 2 - corner_x;
            int32_t offset_y = y * subpixel + subpixel / 2 - corner_y;
            int32_t distance = int32_t(square_root(uint64_t(int64_t(offset_x) * offset_x + int64_t(offset_y) * offset_y)));
            blend_pixel(target, x, y, colour, clamp_coverage(radius + subpixel / 2 - distance));
        }
    }
}

}
