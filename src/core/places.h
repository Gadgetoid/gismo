#pragma once

#include <stdint.h>
#include <stdio.h>

namespace gismo {

constexpr int place_category_count = 4;
constexpr int max_place_kinds = 64;

class place_index;

struct nearby_place {
    const place_index *source;
    uint32_t index;
    double distance;
};

class place_index {
public:
    place_index();
    ~place_index();

    bool load(FILE *file);
    bool loaded() const {
        return place_count > 0;
    }

    uint32_t count() const {
        return place_count;
    }

    double east(uint32_t index) const;
    double north(uint32_t index) const;
    const char *name(uint32_t index) const;
    const char *kind_label(uint32_t index) const;
    int category(uint32_t index) const;

    int nearest(double east, double north, int wanted_category, nearby_place *results, int count, int capacity) const;

private:
    struct record {
        int32_t east;
        int32_t north;
        uint16_t kind;
        uint16_t reserved;
        uint32_t name;
    };

    void release();

    uint8_t *data;
    uint32_t place_count;
    int32_t west;
    int32_t south;
    int32_t cell_size;
    int32_t columns;
    int32_t rows;
    const uint32_t *cells;
    const record *records;
    const char *names;
    uint32_t names_length;
    int kind_count;
    char kind_labels[max_place_kinds][32];
    uint8_t kind_categories[max_place_kinds];
};

}
