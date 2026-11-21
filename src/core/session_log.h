#pragma once

#include <stdio.h>

#include "nmea.h"

namespace gismo {

class track_writer {
public:
    track_writer();
    ~track_writer();

    bool open(FILE *file, const char *name);
    void add(const gps_state &state);
    void close();

    int points() const {
        return point_count;
    }

private:
    void write_footer();

    FILE *file;
    int point_count;
};

class event_writer {
public:
    event_writer();
    ~event_writer();

    bool open(FILE *file);
    void add(const gps_state &state, const char *event, const char *waypoint, const char *detail);
    void close();

private:
    FILE *file;
};

constexpr double metres_per_hdop = 5.0;

void format_utc(const utc_time &time, char *text, int text_size);
double position_error_metres(const gps_state &state);
bool moved_far_enough(double moved_metres, double spacing_metres, const gps_state &state);

}
