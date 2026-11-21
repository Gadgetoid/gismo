#pragma once

#include <stdint.h>

namespace gismo {

struct colour_filter {
    int matrix[3][3];
    uint8_t curves[3][256];
};

bool parse_filter(const char *text, int length, colour_filter &result);

void build_filter_table(const colour_filter &filter, uint16_t *table);

}
