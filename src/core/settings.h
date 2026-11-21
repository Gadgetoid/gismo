#pragma once

namespace gismo {

struct settings {
    double track_spacing_metres = 5.0;
    double track_interval_seconds = 15.0;
    double warmth_interval_seconds = 8.0;
    double warmth_threshold_metres = 4.0;
    double route_refresh_metres = 20.0;
    double clock_wait_seconds = 120.0;
    double pan_mode_timeout_seconds = 3.0;
    double metres_per_step = 0.6;
    double max_drag_jump_pixels = 80.0;
    double jpeg_quality = 85.0;
    bool log_input = false;
};

int apply_settings(const char *text, int length, settings &target);

}
