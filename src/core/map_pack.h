#pragma once

#include <stdint.h>
#include <stdio.h>

namespace gismo {

constexpr int map_tile_size = 256;
constexpr int map_tile_shift = 8;
constexpr int map_max_levels = 16;

struct map_level {
    uint32_t millimetres_per_pixel;
    int32_t west;
    int32_t north;
    uint32_t columns;
    uint32_t rows;
    uint32_t index_offset;
    uint32_t *index;

    int span() const {
        return int(millimetres_per_pixel * map_tile_size / 1000);
    }
};

struct map_tile {
    int level;
    int column;
    int row;
    bool transparent;
    int colour_count;
    uint16_t palette[256];
    uint8_t pixels[map_tile_size * map_tile_size];
};

class map_pack {
public:
    map_pack();
    ~map_pack();

    bool open(FILE *file);
    void close();

    int level_count() const {
        return levels_used;
    }

    const map_level &level(int number) const {
        return levels[number];
    }

    const char *name() const {
        return name_text;
    }

    const char *attribution() const {
        return attribution_text;
    }

    bool has_tile(int level, int column, int row) const;
    bool read_tile(int level, int column, int row, map_tile &tile);

private:
    bool locate(int level, int column, int row, uint32_t &offset, uint32_t &length) const;

    FILE *file;
    map_level levels[map_max_levels];
    int levels_used;
    char name_text[64];
    char attribution_text[160];
    uint8_t *buffer;
    uint32_t buffer_size;
};

class map_tile_cache {
public:
    explicit map_tile_cache(int capacity);
    ~map_tile_cache();

    const map_tile *get(map_pack &pack, int level, int column, int row);
    void clear();

    uint32_t reads() const {
        return tile_reads;
    }

private:
    struct slot {
        map_tile *tile;
        int level;
        int column;
        int row;
        bool used;
        bool missing;
        uint32_t last_used;
    };

    slot *slots;
    int capacity;
    uint32_t clock;
    uint32_t tile_reads;
};

struct framebuffer {
    uint16_t *pixels;
    int width;
    int height;
    int stride;
};

struct map_view {
    double centre_east;
    double centre_north;
    double metres_per_pixel;
};

enum class map_fill {
    background,
    keep,
};

int best_level(const map_pack &pack, double metres_per_pixel);

bool draw_map(map_pack &pack, map_tile_cache &cache, const map_view &view, framebuffer &target, map_fill fill, uint16_t background);

void map_to_screen(const map_view &view, const framebuffer &target, double east, double north, int &x, int &y);

void screen_to_map(const map_view &view, const framebuffer &target, int x, int y, double &east, double &north);

}
