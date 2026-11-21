#include "map_pack.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "inflate.h"

namespace gismo {

namespace {

constexpr uint32_t header_size = 32;
constexpr uint32_t level_record_size = 32;

uint16_t read_u16(const uint8_t *data) {
    return uint16_t(data[0] | data[1] << 8);
}

uint32_t read_u32(const uint8_t *data) {
    return uint32_t(data[0]) | uint32_t(data[1]) << 8 | uint32_t(data[2]) << 16 | uint32_t(data[3]) << 24;
}

void copy_text_value(const char *text, uint32_t length, const char *key, char *output, int output_size) {
    output[0] = 0;
    size_t key_length = strlen(key);
    uint32_t start = 0;
    while (start < length) {
        uint32_t end = start;
        while (end < length && text[end] != '\n') {
            end++;
        }
        if (end - start > key_length && memcmp(text + start, key, key_length) == 0 && text[start + key_length] == '=') {
            uint32_t value_start = start + uint32_t(key_length) + 1;
            uint32_t value_length = end - value_start;
            if (value_length >= uint32_t(output_size)) {
                value_length = output_size - 1;
            }
            memcpy(output, text + value_start, value_length);
            output[value_length] = 0;
            return;
        }
        start = end + 1;
    }
}

}

map_pack::map_pack() : file(nullptr), levels_used(0), buffer(nullptr), buffer_size(0) {
    memset(levels, 0, sizeof(levels));
    name_text[0] = 0;
    attribution_text[0] = 0;
}

map_pack::~map_pack() {
    close();
}

void map_pack::close() {
    for (int number = 0; number < levels_used; number++) {
        free(levels[number].index);
        levels[number].index = nullptr;
    }
    levels_used = 0;
    free(buffer);
    buffer = nullptr;
    buffer_size = 0;
    if (file) {
        fclose(file);
        file = nullptr;
    }
}

bool map_pack::open(FILE *opened) {
    close();
    file = opened;
    if (file == nullptr) {
        return false;
    }

    uint8_t header[header_size];
    if (fread(header, 1, header_size, file) != header_size || memcmp(header, "GISMOMAP", 8) != 0) {
        close();
        return false;
    }
    if (read_u16(header + 8) != 1 || read_u16(header + 10) != map_tile_size) {
        close();
        return false;
    }
    int count = read_u16(header + 12);
    uint32_t level_table_offset = read_u32(header + 16);
    uint32_t text_offset = read_u32(header + 20);
    uint32_t text_length = read_u32(header + 24);
    if (count < 1 || count > map_max_levels) {
        close();
        return false;
    }

    char *text = static_cast<char *>(malloc(text_length + 1));
    if (text == nullptr || fseek(file, long(text_offset), SEEK_SET) != 0 || fread(text, 1, text_length, file) != text_length) {
        free(text);
        close();
        return false;
    }
    copy_text_value(text, text_length, "name", name_text, sizeof(name_text));
    copy_text_value(text, text_length, "attribution", attribution_text, sizeof(attribution_text));
    free(text);

    for (int number = 0; number < count; number++) {
        uint8_t record[level_record_size];
        if (fseek(file, long(level_table_offset + number * level_record_size), SEEK_SET) != 0 || fread(record, 1, level_record_size, file) != level_record_size) {
            close();
            return false;
        }
        map_level &level = levels[number];
        level.millimetres_per_pixel = read_u32(record);
        level.west = int32_t(read_u32(record + 4));
        level.north = int32_t(read_u32(record + 8));
        level.columns = read_u32(record + 12);
        level.rows = read_u32(record + 16);
        level.index_offset = read_u32(record + 20);
        uint32_t entries = level.columns * level.rows * 2;
        level.index = static_cast<uint32_t *>(malloc(entries * sizeof(uint32_t)));
        levels_used = number + 1;
        if (level.index == nullptr || fseek(file, long(level.index_offset), SEEK_SET) != 0 || fread(level.index, sizeof(uint32_t), entries, file) != entries) {
            close();
            return false;
        }
    }
    return true;
}

bool map_pack::locate(int level_number, int column, int row, uint32_t &offset, uint32_t &length) const {
    if (level_number < 0 || level_number >= levels_used) {
        return false;
    }
    const map_level &level = levels[level_number];
    int span = level.span();
    int first_column = level.west / span;
    int top_row = level.north / span - 1;
    int index_column = column - first_column;
    int index_row = top_row - row;
    if (index_column < 0 || index_row < 0 || index_column >= int(level.columns) || index_row >= int(level.rows)) {
        return false;
    }
    const uint32_t *entry = level.index + (index_row * level.columns + index_column) * 2;
    offset = entry[0];
    length = entry[1];
    return length != 0;
}

bool map_pack::has_tile(int level, int column, int row) const {
    uint32_t offset;
    uint32_t length;
    return locate(level, column, row, offset, length);
}

bool map_pack::read_tile(int level, int column, int row, map_tile &tile) {
    uint32_t offset;
    uint32_t length;
    if (!locate(level, column, row, offset, length) || length < 4) {
        return false;
    }
    if (length > buffer_size) {
        uint8_t *grown = static_cast<uint8_t *>(realloc(buffer, length));
        if (grown == nullptr) {
            return false;
        }
        buffer = grown;
        buffer_size = length;
    }
    if (fseek(file, long(offset), SEEK_SET) != 0 || fread(buffer, 1, length, file) != length) {
        return false;
    }

    tile.level = level;
    tile.column = column;
    tile.row = row;
    tile.transparent = buffer[0] & 1;
    tile.colour_count = buffer[1] + 1;
    uint32_t pixels_offset = 2 + tile.colour_count * 2;
    if (pixels_offset > length) {
        return false;
    }
    for (int index = 0; index < tile.colour_count; index++) {
        tile.palette[index] = read_u16(buffer + 2 + index * 2);
    }
    if (tile.colour_count == 1) {
        memset(tile.pixels, 0, sizeof(tile.pixels));
        return true;
    }
    return zlib_inflate(buffer + pixels_offset, int(length - pixels_offset), tile.pixels, sizeof(tile.pixels));
}

map_tile_cache::map_tile_cache(int capacity) : capacity(capacity), clock(0), tile_reads(0) {
    slots = static_cast<slot *>(calloc(capacity, sizeof(slot)));
}

map_tile_cache::~map_tile_cache() {
    for (int index = 0; index < capacity; index++) {
        free(slots[index].tile);
    }
    free(slots);
}

void map_tile_cache::clear() {
    for (int index = 0; index < capacity; index++) {
        slots[index].used = false;
    }
}

const map_tile *map_tile_cache::get(map_pack &pack, int level, int column, int row) {
    clock++;
    slot *oldest = &slots[0];
    for (int index = 0; index < capacity; index++) {
        slot &candidate = slots[index];
        if (candidate.used && candidate.level == level && candidate.column == column && candidate.row == row) {
            candidate.last_used = clock;
            return candidate.missing ? nullptr : candidate.tile;
        }
        if (!candidate.used || (oldest->used && candidate.last_used < oldest->last_used)) {
            oldest = &candidate;
        }
    }

    oldest->used = true;
    oldest->level = level;
    oldest->column = column;
    oldest->row = row;
    oldest->last_used = clock;
    oldest->missing = true;
    if (!pack.has_tile(level, column, row)) {
        return nullptr;
    }
    if (oldest->tile == nullptr) {
        oldest->tile = static_cast<map_tile *>(malloc(sizeof(map_tile)));
        if (oldest->tile == nullptr) {
            oldest->used = false;
            return nullptr;
        }
    }
    tile_reads++;
    if (!pack.read_tile(level, column, row, *oldest->tile)) {
        return nullptr;
    }
    oldest->missing = false;
    return oldest->tile;
}

int best_level(const map_pack &pack, double metres_per_pixel) {
    int best = 0;
    double best_distance = 1e9;
    for (int number = 0; number < pack.level_count(); number++) {
        double ratio = pack.level(number).millimetres_per_pixel / 1000.0 / metres_per_pixel;
        double distance = fabs(log(ratio));
        if (distance < best_distance - 1e-6) {
            best = number;
            best_distance = distance;
        }
    }
    return best;
}

bool draw_map(map_pack &pack, map_tile_cache &cache, const map_view &view, framebuffer &target, map_fill fill, uint16_t background) {
    int level_number = best_level(pack, view.metres_per_pixel);
    const map_level &level = pack.level(level_number);
    double level_metres_per_pixel = level.millimetres_per_pixel / 1000.0;
    double ratio = view.metres_per_pixel / level_metres_per_pixel;
    int32_t step = int32_t(floor(ratio * 65536 + 0.5));
    double left = view.centre_east / level_metres_per_pixel - target.width / 2.0 * ratio;
    double top = view.centre_north / level_metres_per_pixel + target.height / 2.0 * ratio;
    int64_t left_fixed = int64_t(floor(left * 65536 + 0.5));
    int64_t top_fixed = int64_t(floor(top * 65536 + 0.5));
    int32_t left_pixel = int32_t(left_fixed >> 16);
    int32_t left_fraction = int32_t(left_fixed & 0xFFFF);
    int32_t top_pixel = int32_t(top_fixed >> 16);
    int32_t top_fraction = int32_t(top_fixed & 0xFFFF);
    bool drew_anything = false;

    for (int screen_y = 0; screen_y < target.height; screen_y++) {
        uint16_t *output = target.pixels + screen_y * target.stride;
        int32_t north_pixel = top_pixel + ((top_fraction - screen_y * step - step / 2) >> 16);
        int row = north_pixel >> map_tile_shift;
        int tile_y = map_tile_size - 1 - (north_pixel & (map_tile_size - 1));

        int current_column = 0;
        const map_tile *tile = nullptr;
        bool have_tile = false;
        int32_t offset = left_fraction + step / 2;
        for (int screen_x = 0; screen_x < target.width; screen_x++, offset += step) {
            int32_t east_pixel = left_pixel + (offset >> 16);
            int column = east_pixel >> map_tile_shift;
            if (!have_tile || column != current_column) {
                tile = cache.get(pack, level_number, column, row);
                current_column = column;
                have_tile = true;
            }
            if (tile == nullptr) {
                if (fill == map_fill::background) {
                    output[screen_x] = background;
                }
                continue;
            }
            uint8_t index = tile->pixels[tile_y * map_tile_size + (east_pixel & (map_tile_size - 1))];
            if (tile->transparent && index == 0) {
                if (fill == map_fill::background) {
                    output[screen_x] = background;
                }
                continue;
            }
            output[screen_x] = tile->palette[index];
            drew_anything = true;
        }
    }
    return drew_anything;
}

void map_to_screen(const map_view &view, const framebuffer &target, double east, double north, int &x, int &y) {
    x = int(floor((east - view.centre_east) / view.metres_per_pixel + target.width / 2.0));
    y = int(floor((view.centre_north - north) / view.metres_per_pixel + target.height / 2.0));
}

void screen_to_map(const map_view &view, const framebuffer &target, int x, int y, double &east, double &north) {
    east = view.centre_east + (x + 0.5 - target.width / 2.0) * view.metres_per_pixel;
    north = view.centre_north - (y + 0.5 - target.height / 2.0) * view.metres_per_pixel;
}

}
