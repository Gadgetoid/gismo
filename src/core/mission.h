#pragma once

#include <stdio.h>

namespace gismo {

constexpr int max_waypoints = 32;
constexpr int max_choices = 6;

enum class task_kind {
    visit,
    photo,
    choice,
    count,
    via,
};

struct waypoint {
    char name[48];
    double latitude;
    double longitude;
    double easting;
    double northing;
    int radius;
    task_kind task;
    bool route;
    char clue[256];
    char prompt[160];
    char photo[64];
    char choices[max_choices][40];
    int choice_count;
    int answer;
    bool done;
    int response;
    char result[64];
    char done_time[32];
};

struct mission {
    char title[64];
    char intro[256];
    waypoint waypoints[max_waypoints];
    int count;
};

bool parse_mission(const char *text, int length, mission &result, char *error, int error_size);

int apply_progress(const char *text, int length, mission &target);
void write_progress(FILE *file, const mission &source);
int completed_waypoints(const mission &source);
int stop_count(const mission &source);
int completed_stops(const mission &source);
int stop_number(const mission &source, int index);

const char *task_name(task_kind task);

double grid_distance(double from_east, double from_north, double to_east, double to_north);
double grid_bearing(double from_east, double from_north, double to_east, double to_north);

}
