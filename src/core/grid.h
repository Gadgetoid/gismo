#pragma once

namespace gismo {

struct grid_position {
    double easting;
    double northing;
};

grid_position wgs84_to_british_national_grid(double latitude, double longitude, double height = 0);

bool format_grid_reference(const grid_position &position, int digits, char *text, int text_size);

}
