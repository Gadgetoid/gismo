#include "settings.h"

#include <stdlib.h>
#include <string.h>

namespace gismo {

namespace {

struct number_setting {
    const char *name;
    double settings::*member;
    double minimum;
    double maximum;
};

constexpr number_setting number_settings[] = {
    {"track_spacing_metres", &settings::track_spacing_metres, 1, 100},
    {"track_interval_seconds", &settings::track_interval_seconds, 1, 600},
    {"warmth_interval_seconds", &settings::warmth_interval_seconds, 1, 60},
    {"warmth_threshold_metres", &settings::warmth_threshold_metres, 1, 50},
    {"route_refresh_metres", &settings::route_refresh_metres, 5, 500},
    {"clock_wait_seconds", &settings::clock_wait_seconds, 0, 3600},
    {"pan_mode_timeout_seconds", &settings::pan_mode_timeout_seconds, 1, 60},
    {"metres_per_step", &settings::metres_per_step, 0.2, 2},
    {"max_drag_jump_pixels", &settings::max_drag_jump_pixels, 10, 320},
    {"jpeg_quality", &settings::jpeg_quality, 10, 100},
};

char *trim(char *text) {
    while (*text == ' ' || *text == '\t') {
        text++;
    }
    char *end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) {
        *--end = 0;
    }
    return text;
}

bool parse_flag(const char *value, bool &result) {
    if (strcmp(value, "1") == 0 || strcmp(value, "true") == 0 || strcmp(value, "yes") == 0 || strcmp(value, "on") == 0) {
        result = true;
        return true;
    }
    if (strcmp(value, "0") == 0 || strcmp(value, "false") == 0 || strcmp(value, "no") == 0 || strcmp(value, "off") == 0) {
        result = false;
        return true;
    }
    return false;
}

bool apply_setting(const char *key, const char *value, settings &target) {
    if (strcmp(key, "log_input") == 0) {
        return parse_flag(value, target.log_input);
    }
    for (const number_setting &setting : number_settings) {
        if (strcmp(key, setting.name) != 0) {
            continue;
        }
        char *end = nullptr;
        double number = strtod(value, &end);
        if (end == value || *end != 0 || number < setting.minimum || number > setting.maximum) {
            return false;
        }
        target.*setting.member = number;
        return true;
    }
    return false;
}

}

int apply_settings(const char *text, int length, settings &target) {
    int position = 0;
    int applied = 0;
    while (position < length) {
        char line[200];
        int line_length = 0;
        while (position < length && text[position] != '\n') {
            if (line_length < int(sizeof(line)) - 1) {
                line[line_length++] = text[position];
            }
            position++;
        }
        position++;
        line[line_length] = 0;
        char *content = trim(line);
        char *equals = strchr(content, '=');
        if (*content == '#' || equals == nullptr) {
            continue;
        }
        *equals = 0;
        if (apply_setting(trim(content), trim(equals + 1), target)) {
            applied++;
        }
    }
    return applied;
}

}
