#pragma once

#include <stdint.h>

#include "nmea.h"

namespace gismo {

struct photo_tag {
    bool has_position;
    double latitude;
    double longitude;
    double altitude;
    bool has_time;
    utc_time time;
};

int encode_jpeg(const uint16_t *pixels, int width, int height, int quality, const photo_tag *tag, uint8_t *output, int capacity);

}
