#include "app.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <string>

#include "bitmap_file.h"
#include "shim.h"

namespace gismo {

application app;

void log_open(const wchar_t *) {
}

void log_line(const wchar_t *, ...) {
}

void log_close() {
}

gps_port::gps_port() {
}

gps_port::~gps_port() {
}

bool gps_port::start(const wchar_t *, DWORD, HWND, UINT) {
    return false;
}

bool gps_port::start_replay(const wchar_t *, HWND, UINT) {
    return false;
}

void gps_port::stop() {
}

gps_state gps_port::snapshot() {
    return app.location.gps_now;
}

}

using namespace gismo;

namespace {

bool camera_running;
int scene_number;
std::string output_folder;
std::string card;
int shots;
int failures;

void expect(bool ok, const char *what) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

uint16_t pack565(double red, double green, double blue) {
    auto clamp = [](double value) { return value < 0 ? 0 : value > 255 ? 255 : int(value); };
    return uint16_t((clamp(red) >> 3) << 11 | (clamp(green) >> 2) << 5 | clamp(blue) >> 3);
}

void paint_scene(uint16_t *pixels, int number) {
    double sun_x = 60 + number * 37 % 200;
    for (int y = 0; y < camera_height; y++) {
        for (int x = 0; x < camera_width; x++) {
            double hills = 150 + 18 * sin(x / 37.0 + number) + 9 * sin(x / 13.0 + number * 2);
            double near_hill = 190 + 22 * sin(x / 51.0 + number * 3);
            double red;
            double green;
            double blue;
            if (y < hills) {
                double height = y / hills;
                red = 120 + 90 * height;
                green = 170 + 60 * height;
                blue = 240 - 20 * height;
                double cloud = sin(x / 23.0 + number) * sin(y / 9.0 + number) + sin(x / 41.0) * 0.6;
                if (cloud > 0.9 && y < 90) {
                    red = green = blue = 245;
                }
                double sun = hypot(x - sun_x, y - 40);
                if (sun < 18) {
                    red = 255;
                    green = 236;
                    blue = 150;
                }
            } else if (y < near_hill) {
                red = 70 + 0.2 * y;
                green = 140 + 0.15 * y;
                blue = 60;
            } else {
                red = 50 + 30 * sin(x / 7.0 + y / 5.0);
                green = 115 + 25 * sin(x / 11.0 - y / 3.0);
                blue = 40;
            }
            double path = fabs(x - (160 + (y - 240) * 0.9 * (number % 2 ? 1 : -1)));
            if (y > near_hill && path < (y - near_hill) * 0.6 + 4) {
                red = 196;
                green = 170;
                blue = 120;
            }
            double tree = hypot(x - (40 + number * 53 % 200), y - (hills - 8));
            if (tree < 20) {
                red = 40;
                green = 95;
                blue = 45;
            }
            pixels[y * camera_width + x] = pack565(red, green, blue);
        }
    }
}

void save_frame(const char *name) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%02d-%s.ppm", output_folder.c_str(), ++shots, name);
    render();
    FILE *file = fopen(path, "wb");
    fprintf(file, "P6\n%d %d\n255\n", app.ui.width, app.ui.height);
    for (int index = 0; index < app.ui.width * app.ui.height; index++) {
        uint16_t pixel = app.ui.bits[index];
        unsigned char rgb[3] = {
            static_cast<unsigned char>(((pixel >> 11) & 0x1F) * 255 / 31),
            static_cast<unsigned char>(((pixel >> 5) & 0x3F) * 255 / 63),
            static_cast<unsigned char>((pixel & 0x1F) * 255 / 31),
        };
        fwrite(rgb, 1, 3, file);
    }
    fclose(file);
    printf("%s\n", path);
}

void set_time(int hour, int minute, int second) {
    SYSTEMTIME now = {2026, 11, 6, 14, WORD(hour), WORD(minute), WORD(second), 0};
    SetSystemTime(&now);
    app.location.gps_now.has_time = true;
    app.location.gps_now.has_date = true;
    app.location.gps_now.time = {2026, 11, 14, hour, minute, second, 0};
}

void set_satellites() {
    gps_state &gps = app.location.gps_now;
    const int sky[][4] = {{2, 64, 48, 44}, {5, 31, 112, 38}, {7, 18, 300, 30}, {9, 55, 205, 42}, {13, 9, 160, 22}, {15, 42, 262, 36}, {18, 72, 330, 46}, {20, 24, 20, 33}, {24, 12, 78, 0}, {27, 36, 140, 40}, {29, 6, 240, 18}, {30, 48, 4, 41}};
    gps.satellite_count = 0;
    gps.satellites_used = 0;
    for (const auto &entry : sky) {
        satellite &slot = gps.satellites[gps.satellite_count++];
        slot = {entry[0], entry[1], entry[2], entry[3], entry[3] >= 30};
        gps.satellites_used += slot.used;
    }
}

void grid_to_wgs84(double east, double north, double &latitude, double &longitude) {
    latitude = 52.2;
    longitude = 0.12;
    for (int iteration = 0; iteration < 8; iteration++) {
        grid_position guess = wgs84_to_british_national_grid(latitude, longitude);
        latitude += (north - guess.northing) / 111320.0;
        longitude += (east - guess.easting) / (111320.0 * cos(latitude * M_PI / 180));
    }
}

void move_to(double east, double north) {
    double latitude = 0;
    double longitude = 0;
    grid_to_wgs84(east, north, latitude, longitude);
    gps_state &gps = app.location.gps_now;
    gps.has_position = true;
    gps.latitude = latitude;
    gps.longitude = longitude;
    gps.altitude = 14.2;
    gps.mode = fix_mode::three_d;
    gps.fix_quality = 1;
    gps.hdop = 0.9;
    gps.pdop = 1.6;
    gps.vdop = 1.3;
    app.location.position = wgs84_to_british_national_grid(latitude, longitude);
    if (app.map.follow) {
        app.map.centre_east = app.location.position.easting;
        app.map.centre_north = app.location.position.northing;
    }
}

void walk(double from_east, double from_north, double to_east, double to_north, int steps) {
    for (int step = 0; step <= steps; step++) {
        double fraction = double(step) / steps;
        double wobble = sin(step * 0.7) * 3;
        move_to(from_east + (to_east - from_east) * fraction + wobble, from_north + (to_north - from_north) * fraction);
        shim_set_tick(GetTickCount() + 4000);
        record_position();
        const waypoint *target = current_target();
        if (target) {
            update_warmth(grid_distance(app.location.position.easting, app.location.position.northing, target->easting, target->northing));
        }
    }
    update_route();
}

void write_text(const std::string &path, const std::string &text) {
    FILE *file = fopen(path.c_str(), "wb");
    fwrite(text.data(), 1, text.size(), file);
    fclose(file);
}

std::string read_text(const std::string &path) {
    FILE *file = fopen(path.c_str(), "rb");
    std::string text;
    char buffer[4096];
    size_t read;
    while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        text.append(buffer, read);
    }
    fclose(file);
    return text;
}

int mission_index(const wchar_t *stem) {
    for (int index = 0; index < app.mission.mission_count; index++) {
        if (lstrcmpW(app.mission.missions[index].stem, stem) == 0) {
            return index;
        }
    }
    return -1;
}

void prepare_card(const std::string &repository) {
    std::string missions = card + "/Missions";
    mkdir(missions.c_str(), 0755);
    std::string cambridge = read_text(repository + "/missions/cambridge.txt");
    std::string section = "[Round Church]\n";
    cambridge.insert(cambridge.find(section) + section.size(), "photo = round-church.bmp\n");
    write_text(missions + "/cambridge.txt", cambridge);
    static uint16_t hint[camera_width * camera_height];
    paint_scene(hint, 7);
    wchar_t hint_path[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, (missions + "/round-church.bmp").c_str(), -1, hint_path, MAX_PATH);
    write_bmp565(hint_path, hint, camera_width, camera_height);
    write_text(missions + "/parkers-piece.txt",
        "title = Parker's Piece loop\n"
        "intro = A short loop with route points to keep you on the paths.\n\n"
        "[Reality Checkpoint]\nlocation = 52.202195, 0.128203\ntask = visit\n\n"
        "[]\nlocation = 52.201300, 0.126500\ntask = via\nroute = yes\n\n"
        "[]\nlocation = 52.200400, 0.127900\ntask = via\n\n"
        "[Fenner's]\nlocation = 52.199400, 0.129700\ntask = choice\nprompt = What sport is played here?\nchoices = Cricket | Golf | Rugby\nanswer = Cricket\n\n"
        "[Long question]\nlocation = 52.199000, 0.130500\ntask = choice\nprompt = Look at the old sign by the gate. Which of these words can you spot on it, reading from the top all the way down?\nchoices = Cricket | Ground | Pavilion | Members | Gate | Club\nanswer = Ground\n\n"
        "[Long count]\nlocation = 52.198800, 0.131000\ntask = count\nprompt = Stand by the scoreboard and count every window you can see on the pavilion, upstairs and downstairs, including the little round ones.\n");
}

}

bool camera::available() const {
    return true;
}

camera::camera() : driver(nullptr) {
}

camera::~camera() {
}

bool camera::start(HWND, UINT) {
    camera_running = true;
    return true;
}

void camera::stop() {
    camera_running = false;
}

bool camera::running() const {
    return camera_running;
}

bool camera::copy_frame(uint16_t *target) {
    paint_scene(target, scene_number);
    return true;
}

bool camera::set_zoom(int) {
    return true;
}

int camera::zoom() const {
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: screens REPOSITORY CARD OUTPUT\n");
        return 1;
    }
    std::string repository = argv[1];
    card = argv[2];
    output_folder = argv[3];
    shim_set_card_folder(card.c_str());
    prepare_card(repository);

    app.ui.small_font = make_font(12, false);
    app.ui.medium_font = make_font(15, true);
    app.ui.large_font = make_font(18, true);
    app.ui.huge_font = make_font(26, true);
    app.map.follow = true;
    app.map.zoom = 2;
    app.map.overview_pack = -1;
    app.mission.target = -1;
    app.map.route_target = -1;
    app.map.route_goal_node = -1;
    create_surface();
    load_maps();
    load_filters();
    app.camera.filter_index = -1;
    app.camera.camera_task = -1;
    scan_missions();
    app.outing.started_tick = GetTickCount();
    set_time(10, 2, 0);
    set_satellites();
    app.outing.clock_set = true;
    app.location.gps_connected = true;
    start_session();

    select_mission(mission_index(L"cambridge"));
    const waypoint &church = app.mission.quest.waypoints[0];
    move_to(church.easting + 40, church.northing - 260);
    app.ui.current = screen::clue;
    save_frame("clue");

    open_hint();
    save_frame("photo-hint");

    app.ui.current = screen::map;
    app.map.route_hint = true;
    walk(church.easting + 120, church.northing - 420, church.easting + 25, church.northing - 150, 14);
    set_time(10, 6, 30);
    save_frame("map-route");

    move_to(church.easting + 5, church.northing - 8);
    open_task(0);
    save_frame("task-photo");

    open_camera(0);
    select_filter(3);
    scene_number = 1;
    shim_set_tick(GetTickCount() + 5000);
    check_arrival();
    expect(app.ui.current == screen::camera, "arriving again doesn't close the camera");
    save_frame("camera");

    set_time(10, 8, 12);
    take_photo();
    shim_set_tick(GetTickCount() + 5000);
    const waypoint &chapel = app.mission.quest.waypoints[1];
    app.map.zoom = 3;
    walk(church.easting, church.northing, chapel.easting + 30, chapel.northing + 120, 20);
    app.ui.current = screen::map;
    set_time(10, 14, 40);
    save_frame("map-walking");

    mark_done(app.mission.quest.waypoints[1], "");
    move_to(app.mission.quest.waypoints[2].easting, app.mission.quest.waypoints[2].northing);
    open_task(2);
    app.mission.choice_selected = 0;
    save_frame("task-choice");

    app.mission.choice_feedback = 1;
    app.mission.choice_selected = 1;
    save_frame("task-choice-wrong");

    app.mission.choice_feedback = -1;
    finish_task("Grasshopper");
    open_task(3);
    app.mission.count_value = 4;
    save_frame("task-count");

    show_page(screen::list);
    save_frame("mission-list");

    show_page(screen::sky);
    save_frame("sky");

    app.outing.distance_walked = 1240;
    shim_set_tick(app.outing.started_tick + 26 * 60000);
    show_page(screen::list);
    RECT summary_button = header_summary_rect();
    on_tap(nullptr, (summary_button.left + summary_button.right) / 2, (summary_button.top + summary_button.bottom) / 2);
    expect(app.ui.current == screen::summary, "the header icon opens the outing summary");
    save_frame("outing-so-far");

    app.camera.camera_task = -1;
    for (int photo = 0; photo < 5; photo++) {
        open_camera(-1);
        select_filter(photo * 2 % app.camera.filter_count - 1);
        scene_number = photo + 2;
        set_time(10, 20 + photo, photo * 7);
        take_photo();
        close_camera(screen::map);
    }
    show_page(screen::photos);
    save_frame("photos");

    open_photo(1);
    save_frame("photo-view");

    for (int index = 0; index < app.mission.quest.count; index++) {
        if (!app.mission.quest.waypoints[index].done) {
            mark_done(app.mission.quest.waypoints[index], "");
        }
    }
    app.outing.distance_walked = 2380;
    shim_set_tick(app.outing.started_tick + 47 * 60000);
    set_time(10, 49, 0);
    app.ui.current = screen::summary;
    app.outing.saved_note[0] = 0;
    expect(save_outing(), "the outing zip saves");
    save_frame("complete");

    open_picker();
    save_frame("missions");

    select_mission(-1);
    app.map.follow = true;
    move_to(545330, 257990);
    show_page(screen::list);
    save_frame("nearby");

    app.explore.category = 0;
    refresh_nearby(true);
    save_frame("nearby-play");

    choose_nearby(0);
    app.map.follow = true;
    app.map.zoom = 3;
    app.map.route_hint = true;
    walk(545330, 257990, 545310, 258000, 3);
    app.ui.current = screen::map;
    save_frame("explore-map");

    open_found();
    save_frame("found");

    select_mission(mission_index(L"parkers-piece"));
    const waypoint &checkpoint = app.mission.quest.waypoints[0];
    mark_done(app.mission.quest.waypoints[0], "");
    app.mission.target = 1;
    app.map.route_hint = false;
    app.map.zoom = 3;
    move_to(checkpoint.easting + 10, checkpoint.northing + 5);
    app.ui.current = screen::map;
    update_route();
    save_frame("route-points");

    open_task(4);
    app.mission.choice_selected = 2;
    save_frame("task-long-choice");

    open_task(5);
    app.mission.count_value = 12;
    save_frame("task-long-count");

    return failures ? 1 : 0;
}
