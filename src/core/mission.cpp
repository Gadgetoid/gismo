#include "mission.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "grid.h"

namespace gismo {

namespace {

constexpr int default_radius = 25;

void copy_value(char *target, int target_size, const char *value) {
    int written = 0;
    for (const char *character = value; *character && written < target_size - 1; character++) {
        if (character[0] == '\\' && character[1] == 'n') {
            target[written++] = '\n';
            character++;
        } else {
            target[written++] = *character;
        }
    }
    target[written] = 0;
}

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

bool equal_ignoring_case(const char *first, const char *second) {
    while (*first && *second) {
        char a = *first >= 'A' && *first <= 'Z' ? char(*first + 32) : *first;
        char b = *second >= 'A' && *second <= 'Z' ? char(*second + 32) : *second;
        if (a != b) {
            return false;
        }
        first++;
        second++;
    }
    return *first == *second;
}

bool parse_number(const char *text, double &value) {
    char *end;
    value = strtod(text, &end);
    return end != text;
}

bool parse_location(char *value, waypoint &point) {
    char *comma = strchr(value, ',');
    if (comma == nullptr) {
        return false;
    }
    *comma = 0;
    double latitude;
    double longitude;
    if (!parse_number(trim(value), latitude) || !parse_number(trim(comma + 1), longitude)) {
        return false;
    }
    if (latitude < -90 || latitude > 90 || longitude < -180 || longitude > 180) {
        return false;
    }
    point.latitude = latitude;
    point.longitude = longitude;
    grid_position position = wgs84_to_british_national_grid(latitude, longitude);
    point.easting = position.easting;
    point.northing = position.northing;
    return true;
}

void parse_choices(char *value, waypoint &point) {
    point.choice_count = 0;
    char *start = value;
    while (start && point.choice_count < max_choices) {
        char *bar = strchr(start, '|');
        if (bar) {
            *bar = 0;
        }
        char *choice = trim(start);
        if (*choice) {
            copy_value(point.choices[point.choice_count++], sizeof(point.choices[0]), choice);
        }
        start = bar ? bar + 1 : nullptr;
    }
}

}

int stop_count(const mission &source) {
    int stops = 0;
    for (int index = 0; index < source.count; index++) {
        stops += source.waypoints[index].task != task_kind::via;
    }
    return stops;
}

int completed_stops(const mission &source) {
    int done = 0;
    for (int index = 0; index < source.count; index++) {
        done += source.waypoints[index].done && source.waypoints[index].task != task_kind::via;
    }
    return done;
}

int stop_number(const mission &source, int index) {
    if (index < 0 || index >= source.count || source.waypoints[index].task == task_kind::via) {
        return 0;
    }
    int number = 0;
    for (int before = 0; before <= index; before++) {
        number += source.waypoints[before].task != task_kind::via;
    }
    return number;
}

const char *task_name(task_kind task) {
    switch (task) {
    case task_kind::photo:
        return "photo";
    case task_kind::choice:
        return "choice";
    case task_kind::count:
        return "count";
    case task_kind::via:
        return "via";
    default:
        return "visit";
    }
}

bool parse_mission(const char *text, int length, mission &result, char *error, int error_size) {
    memset(&result, 0, sizeof(result));
    error[0] = 0;
    char answers[max_waypoints][40] = {};
    bool located[max_waypoints] = {};
    waypoint *current = nullptr;
    int line_number = 0;
    int position = 0;

    if (length >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) {
        position = 3;
    }

    while (position < length) {
        char line[400];
        int line_length = 0;
        while (position < length && text[position] != '\n') {
            if (line_length < int(sizeof(line)) - 1) {
                line[line_length++] = text[position];
            }
            position++;
        }
        position++;
        line[line_length] = 0;
        line_number++;

        char *content = trim(line);
        if (*content == 0 || *content == '#' || *content == ';') {
            continue;
        }

        if (*content == '[') {
            char *close = strchr(content, ']');
            if (close == nullptr) {
                snprintf(error, error_size, "line %d: missing ]", line_number);
                return false;
            }
            if (result.count >= max_waypoints) {
                snprintf(error, error_size, "line %d: more than %d waypoints", line_number, max_waypoints);
                return false;
            }
            *close = 0;
            current = &result.waypoints[result.count++];
            copy_value(current->name, sizeof(current->name), trim(content + 1));
            current->radius = default_radius;
            current->task = task_kind::visit;
            current->answer = -1;
            current->response = -1;
            continue;
        }

        char *equals = strchr(content, '=');
        if (equals == nullptr) {
            snprintf(error, error_size, "line %d: expected key = value", line_number);
            return false;
        }
        *equals = 0;
        char *key = trim(content);
        char *value = trim(equals + 1);

        if (current == nullptr) {
            if (equal_ignoring_case(key, "title")) {
                copy_value(result.title, sizeof(result.title), value);
            } else if (equal_ignoring_case(key, "intro")) {
                copy_value(result.intro, sizeof(result.intro), value);
            } else {
                snprintf(error, error_size, "line %d: unknown mission key '%s'", line_number, key);
                return false;
            }
            continue;
        }

        int index = int(current - result.waypoints);
        if (equal_ignoring_case(key, "location")) {
            if (!parse_location(value, *current)) {
                snprintf(error, error_size, "line %d: location should be latitude, longitude", line_number);
                return false;
            }
            located[index] = true;
        } else if (equal_ignoring_case(key, "radius")) {
            double radius;
            if (!parse_number(value, radius) || radius < 1) {
                snprintf(error, error_size, "line %d: radius should be a number of metres", line_number);
                return false;
            }
            current->radius = int(radius + 0.5);
        } else if (equal_ignoring_case(key, "task")) {
            if (equal_ignoring_case(value, "visit")) {
                current->task = task_kind::visit;
            } else if (equal_ignoring_case(value, "photo")) {
                current->task = task_kind::photo;
            } else if (equal_ignoring_case(value, "choice")) {
                current->task = task_kind::choice;
            } else if (equal_ignoring_case(value, "count")) {
                current->task = task_kind::count;
            } else if (equal_ignoring_case(value, "via")) {
                current->task = task_kind::via;
            } else {
                snprintf(error, error_size, "line %d: task should be visit, photo, choice, count or via", line_number);
                return false;
            }
        } else if (equal_ignoring_case(key, "route")) {
            if (equal_ignoring_case(value, "yes") || equal_ignoring_case(value, "true")) {
                current->route = true;
            } else if (equal_ignoring_case(value, "no") || equal_ignoring_case(value, "false")) {
                current->route = false;
            } else {
                snprintf(error, error_size, "line %d: route should be yes or no", line_number);
                return false;
            }
        } else if (equal_ignoring_case(key, "clue")) {
            copy_value(current->clue, sizeof(current->clue), value);
        } else if (equal_ignoring_case(key, "prompt")) {
            copy_value(current->prompt, sizeof(current->prompt), value);
        } else if (equal_ignoring_case(key, "photo")) {
            copy_value(current->photo, sizeof(current->photo), value);
        } else if (equal_ignoring_case(key, "choices")) {
            parse_choices(value, *current);
        } else if (equal_ignoring_case(key, "answer")) {
            copy_value(answers[index], sizeof(answers[0]), value);
        } else {
            snprintf(error, error_size, "line %d: unknown waypoint key '%s'", line_number, key);
            return false;
        }
    }

    if (result.count == 0) {
        snprintf(error, error_size, "no waypoints");
        return false;
    }
    for (int index = 0; index < result.count; index++) {
        waypoint &point = result.waypoints[index];
        if (point.name[0] == 0) {
            if (point.task != task_kind::via) {
                snprintf(error, error_size, "waypoint %d needs a name", index + 1);
                return false;
            }
            snprintf(point.name, sizeof(point.name), "Via %d", index + 1);
        }
        if (!located[index]) {
            snprintf(error, error_size, "%s: no location", point.name);
            return false;
        }
        if (point.task == task_kind::choice) {
            if (point.choice_count < 2) {
                snprintf(error, error_size, "%s: a choice task needs at least two choices", point.name);
                return false;
            }
            for (int choice = 0; choice < point.choice_count; choice++) {
                if (equal_ignoring_case(point.choices[choice], answers[index])) {
                    point.answer = choice;
                }
            }
            if (answers[index][0] && point.answer < 0) {
                snprintf(error, error_size, "%s: answer '%s' isn't one of the choices", point.name, answers[index]);
                return false;
            }
        }
    }
    return true;
}

double grid_distance(double from_east, double from_north, double to_east, double to_north) {
    double east = to_east - from_east;
    double north = to_north - from_north;
    return sqrt(east * east + north * north);
}

double grid_bearing(double from_east, double from_north, double to_east, double to_north) {
    double bearing = atan2(to_east - from_east, to_north - from_north) * 180.0 / 3.14159265358979323846;
    return bearing < 0 ? bearing + 360 : bearing;
}

int completed_waypoints(const mission &source) {
    int done = 0;
    for (int index = 0; index < source.count; index++) {
        if (source.waypoints[index].done) {
            done++;
        }
    }
    return done;
}

int apply_progress(const char *text, int length, mission &target) {
    waypoint *current = nullptr;
    int position = 0;
    int applied = 0;
    while (position < length) {
        char line[400];
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
        if (*content == 0 || *content == '#') {
            continue;
        }
        if (*content == '[') {
            char *close = strchr(content, ']');
            current = nullptr;
            if (close == nullptr) {
                continue;
            }
            *close = 0;
            char *name = trim(content + 1);
            for (int index = 0; index < target.count; index++) {
                if (strcmp(target.waypoints[index].name, name) == 0) {
                    current = &target.waypoints[index];
                }
            }
            continue;
        }
        char *equals = strchr(content, '=');
        if (current == nullptr || equals == nullptr) {
            continue;
        }
        *equals = 0;
        char *key = trim(content);
        char *value = trim(equals + 1);
        if (equal_ignoring_case(key, "done")) {
            if (!current->done) {
                applied++;
            }
            current->done = true;
            copy_value(current->done_time, sizeof(current->done_time), value);
        } else if (equal_ignoring_case(key, "answer")) {
            copy_value(current->result, sizeof(current->result), value);
        }
    }
    return applied;
}

void write_progress(FILE *file, const mission &source) {
    fprintf(file, "# GISmo progress for %s. Delete a section, or this file, to do it again.\n", source.title);
    for (int index = 0; index < source.count; index++) {
        const waypoint &point = source.waypoints[index];
        if (!point.done) {
            continue;
        }
        fprintf(file, "\n[%s]\ndone = %s\n", point.name, point.done_time[0] ? point.done_time : "yes");
        if (point.result[0]) {
            fprintf(file, "answer = %s\n", point.result);
        }
    }
}

}
