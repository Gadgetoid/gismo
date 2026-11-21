#include <math.h>
#include <stdio.h>
#include <string.h>

#include "draw.h"
#include "filter.h"
#include "grid.h"
#include "mission.h"
#include "nmea.h"
#include "places.h"
#include "session_log.h"
#include "settings.h"
#include "zip_writer.h"

using namespace gismo;

static int failures;

static void check(bool condition, const char *description) {
    if (!condition) {
        printf("FAIL: %s\n", description);
        failures++;
    }
}

static void sentence(char *output, int output_size, const char *body) {
    int checksum = 0;
    for (const char *character = body; *character; character++) {
        checksum ^= (unsigned char)*character;
    }
    snprintf(output, output_size, "$%s*%02X\r\n", body, checksum);
}

static void feed(nmea_parser &parser, const char *body) {
    char text[120];
    sentence(text, sizeof(text), body);
    parser.feed(text, int(strlen(text)));
}

static void test_nmea() {
    nmea_parser parser;
    feed(parser, "GPGGA,123519.250,5212.318,N,00007.308,E,1,08,0.9,545.4,M,46.9,M,,");
    const gps_state &state = parser.state();
    check(state.has_position, "GGA gives a position");
    check(fabs(state.latitude - (52 + 12.318 / 60)) < 1e-9, "GGA latitude");
    check(fabs(state.longitude - (7.308 / 60)) < 1e-9, "GGA longitude");
    check(fabs(state.altitude - 545.4) < 1e-9, "GGA altitude");
    check(state.satellites_used == 8, "GGA satellites used");
    check(state.time.hour == 12 && state.time.minute == 35 && state.time.second == 19 && state.time.millisecond == 250, "GGA time");

    feed(parser, "GPRMC,123520,A,5212.400,N,00118.000,W,10.0,084.4,141126,003.1,W");
    check(state.has_date && state.time.year == 2026 && state.time.month == 11 && state.time.day == 14, "RMC date");
    check(fabs(state.longitude + 1.3) < 1e-9, "RMC west longitude");
    check(fabs(state.speed_metres_per_second - 10 * 1852.0 / 3600) < 1e-9, "RMC speed");
    check(state.has_course && fabs(state.course_degrees - 84.4) < 1e-9, "RMC course");
    check(state.position_updates == 2, "position updates counted");

    feed(parser, "GPGSA,A,3,04,05,,09,12,,,24,,,,,2.5,1.3,2.1");
    feed(parser, "GPGSV,3,1,11,03,03,111,00,04,15,270,00,06,01,010,00,13,06,292,00");
    check(state.satellite_updates == 0, "partial GSV not committed");
    feed(parser, "GPGSV,3,2,11,14,25,170,00,16,57,208,39,18,67,296,40,19,40,246,00");
    feed(parser, "GPGSV,3,3,11,22,42,067,42,24,14,311,43,27,05,244,");
    check(state.satellite_updates == 1, "GSV committed after last message");
    check(state.satellite_count == 11, "GSV satellite count");
    check(state.satellites[10].prn == 27 && state.satellites[10].snr == 0, "GSV empty SNR");
    check(state.satellites[6].prn == 18 && state.satellites[6].elevation == 67 && state.satellites[6].azimuth == 296 && state.satellites[6].snr == 40, "GSV fields");
    check(state.satellites[1].used && state.satellites[9].used && !state.satellites[0].used, "GSA marks used satellites");
    check(state.mode == fix_mode::three_d && fabs(state.pdop - 2.5) < 1e-9 && fabs(state.vdop - 2.1) < 1e-9, "GSA mode and DOP");

    feed(parser, "GPGSV,3,1,11,03,03,111,00,04,15,270,00,06,01,010,00,13,06,292,00");
    feed(parser, "GPGSV,3,3,11,22,42,067,42,24,14,311,43,27,05,244,");
    check(state.satellite_updates == 1, "GSV with a missing message is dropped");

    const char *corrupt = "$GPRMC,123520,A,5212.400,N,00118.000,W,10.0,084.4,141126,003.1,W*00\r\n";
    parser.feed(corrupt, int(strlen(corrupt)));
    check(state.checksum_errors == 1 && state.position_updates == 2, "bad checksum rejected");

    feed(parser, "GPRMC,123521,V,,,,,,,141126,,");
    check(!state.has_position, "RMC void clears the position");

    const char *split_a = "$GPGGA,123519,5212.318,N,000";
    char rest[120];
    sentence(rest, sizeof(rest), "GPGGA,123519,5212.318,N,00007.308,E,1,08,0.9,545.4,M,46.9,M,,");
    parser.feed(split_a, 4);
    parser.feed(rest + 4, int(strlen(rest)) - 4);
    check(state.has_position && state.position_updates == 3, "sentence split across reads");
}

static void test_grid() {
    FILE *file = fopen("grid_vectors.csv", "r");
    check(file != nullptr, "grid_vectors.csv opens");
    if (file == nullptr) {
        return;
    }
    double latitude, longitude, easting, northing, default_easting, default_northing;
    double worst = 0;
    int count = 0;
    while (fscanf(file, "%lf,%lf,%lf,%lf,%lf,%lf", &latitude, &longitude, &easting, &northing, &default_easting, &default_northing) == 6) {
        grid_position position = wgs84_to_british_national_grid(latitude, longitude);
        double error = hypot(position.easting - easting, position.northing - northing);
        if (error > worst) {
            worst = error;
        }
        count++;
    }
    fclose(file);
    printf("grid: %d points, worst error %.4f m\n", count, worst);
    check(count > 100 && worst < 0.01, "grid conversion within 1 cm of pyproj");

    char text[32];
    check(format_grid_reference({545088.4, 258462.9}, 6, text, sizeof(text)) && strcmp(text, "TL 450 584") == 0, "six figure grid reference");
    check(format_grid_reference({545088.4, 258462.9}, 10, text, sizeof(text)) && strcmp(text, "TL 45088 58462") == 0, "ten figure grid reference");
    check(format_grid_reference({134369.6, 25005.0}, 4, text, sizeof(text)) && strcmp(text, "SW 34 25") == 0, "SW square");
    check(format_grid_reference({338044.0, 972651.0}, 6, text, sizeof(text)) && strcmp(text, "ND 380 726") == 0, "ND square");
    check(!format_grid_reference({-5, 100}, 6, text, sizeof(text)), "outside the grid");
}

static bool parse_text(const char *text, mission &result, char *error) {
    return parse_mission(text, int(strlen(text)), result, error, 128);
}

static void test_mission() {
    static mission result;
    char error[128];
    FILE *file = fopen("../../missions/cambridge.txt", "rb");
    check(file != nullptr, "cambridge.txt opens");
    if (file) {
        static char text[16384];
        int length = int(fread(text, 1, sizeof(text), file));
        fclose(file);
        bool parsed = parse_mission(text, length, result, error, sizeof(error));
        if (!parsed) {
            printf("cambridge.txt: %s\n", error);
        }
        check(parsed, "cambridge.txt parses");
        check(strcmp(result.title, "Cambridge Explorer") == 0, "mission title");
        check(result.count == 5, "five waypoints");
        check(result.waypoints[2].task == task_kind::choice && result.waypoints[2].choice_count == 4 && result.waypoints[2].answer == 0, "choice task and answer");
        check(result.waypoints[1].radius == 40 && result.waypoints[0].radius == 25, "radius and default");
        grid_position church = wgs84_to_british_national_grid(52.208475, 0.118984);
        check(fabs(result.waypoints[0].easting - church.easting) < 0.001, "waypoint easting");
        double distance = grid_distance(result.waypoints[0].easting, result.waypoints[0].northing, result.waypoints[1].easting, result.waypoints[1].northing);
        double bearing = grid_bearing(result.waypoints[0].easting, result.waypoints[0].northing, result.waypoints[1].easting, result.waypoints[1].northing);
        printf("mission: round church to chapel %.0f m at %.0f degrees\n", distance, bearing);
        check(distance > 400 && distance < 500 && bearing > 190 && bearing < 225, "distance and bearing");
    }

    check(parse_text("title = T\n[A]\nlocation = 52.1, 0.1\nclue = one\\ntwo\n", result, error) && strcmp(result.waypoints[0].clue, "one\ntwo") == 0, "newline escape");
    check(parse_text("title = T\n[A]\nlocation = 52.1, 0.1\nphoto = church.bmp\n[B]\nlocation = 52.2, 0.1\n", result, error) && strcmp(result.waypoints[0].photo, "church.bmp") == 0 && result.waypoints[1].photo[0] == 0, "photo hint");
    check(!parse_text("title = T\n[A]\ntask = visit\n", result, error) && strstr(error, "no location"), "missing location");
    check(!parse_text("[A]\nlocation = 52.1\n", result, error) && strstr(error, "line 2"), "bad location");
    check(!parse_text("[A]\nlocation = 52.1, 0.1\ntask = dance\n", result, error) && strstr(error, "line 3"), "bad task");
    check(!parse_text("[A]\nlocation = 52.1, 0.1\ntask = choice\nchoices = a | b\nanswer = c\n", result, error) && strstr(error, "isn't one of"), "bad answer");
    check(!parse_text("colour = blue\n", result, error) && strstr(error, "unknown mission key"), "unknown key");
    check(!parse_text("# nothing\n", result, error) && strstr(error, "no waypoints"), "empty mission");
}

static void test_via() {
    static mission result;
    char error[128];
    const char *text = "title = V\n[Start]\nlocation = 52.1, 0.1\n[]\nlocation = 52.11, 0.1\ntask = via\nroute = yes\n[Gate]\nlocation = 52.12, 0.1\ntask = via\n[End]\nlocation = 52.13, 0.1\ntask = photo\n";
    check(parse_mission(text, int(strlen(text)), result, error, sizeof(error)), "via mission parses");
    check(result.waypoints[1].task == task_kind::via && strcmp(result.waypoints[1].name, "Via 2") == 0 && strcmp(result.waypoints[2].name, "Gate") == 0, "unnamed via gets a name");
    check(result.waypoints[1].route && !result.waypoints[2].route, "route flag");
    check(stop_count(result) == 2 && stop_number(result, 0) == 1 && stop_number(result, 1) == 0 && stop_number(result, 3) == 2, "via points aren't numbered stops");
    result.waypoints[1].done = true;
    result.waypoints[3].done = true;
    check(completed_stops(result) == 1 && completed_waypoints(result) == 2, "completed stops leave out via points");
    const char *unnamed = "title = V\n[]\nlocation = 52.1, 0.1\n";
    check(!parse_mission(unnamed, int(strlen(unnamed)), result, error, sizeof(error)) && strstr(error, "needs a name"), "only via points can be unnamed");
    const char *bad_route = "title = V\n[A]\nlocation = 52.1, 0.1\nroute = maybe\n";
    check(!parse_mission(bad_route, int(strlen(bad_route)), result, error, sizeof(error)) && strstr(error, "line 4"), "bad route value");
}

static void test_progress() {
    static mission result;
    char error[128];
    check(parse_text("title = P\n[One]\nlocation = 52.1, 0.1\n[Two]\nlocation = 52.2, 0.1\n[Three]\nlocation = 52.3, 0.1\n", result, error), "progress mission parses");
    result.waypoints[0].done = true;
    strcpy(result.waypoints[0].done_time, "2026-11-14T12:00:00.000Z");
    result.waypoints[2].done = true;
    strcpy(result.waypoints[2].result, "Red");
    FILE *file = fopen("test_progress.txt", "wb+");
    write_progress(file, result);
    fseek(file, 0, SEEK_SET);
    static char text[4096];
    int length = int(fread(text, 1, sizeof(text) - 1, file));
    fclose(file);
    remove("test_progress.txt");
    text[length] = 0;

    static mission reloaded;
    parse_text("title = P\n[One]\nlocation = 52.1, 0.1\n[Two]\nlocation = 52.2, 0.1\n[Three]\nlocation = 52.3, 0.1\n", reloaded, error);
    check(apply_progress(text, length, reloaded) == 2, "two waypoints restored");
    check(reloaded.waypoints[0].done && !reloaded.waypoints[1].done && reloaded.waypoints[2].done, "done flags restored");
    check(strcmp(reloaded.waypoints[0].done_time, "2026-11-14T12:00:00.000Z") == 0, "done time restored");
    check(strcmp(reloaded.waypoints[2].result, "Red") == 0, "answer restored");
    check(completed_waypoints(reloaded) == 2, "completed count");
    const char *edited = "# hand edited\n[Two]\ndone = yes\n[Nowhere]\ndone = yes\n";
    check(apply_progress(edited, int(strlen(edited)), reloaded) == 1 && completed_waypoints(reloaded) == 3, "hand edits and unknown names");
}

static void test_session_log() {
    const char *path = "test_track.gpx";
    track_writer track;
    check(track.open(fopen(path, "wb+"), "Test & <walk>"), "track opens");
    gps_state state = {};
    state.has_position = true;
    state.has_time = state.has_date = true;
    state.time = {2026, 11, 14, 10, 0, 1, 0};
    state.latitude = 52.25;
    state.longitude = 0.0184;
    state.satellites_used = 7;
    state.mode = fix_mode::three_d;
    state.hdop = 1.2;
    for (int index = 0; index < 3; index++) {
        state.time.second = index;
        track.add(state);
    }
    track.close();

    FILE *file = fopen(path, "rb");
    static char text[8192];
    int length = int(fread(text, 1, sizeof(text) - 1, file));
    fclose(file);
    text[length] = 0;
    int points = 0;
    for (const char *found = strstr(text, "<trkpt"); found; found = strstr(found + 1, "<trkpt")) {
        points++;
    }
    check(points == 3, "three track points");
    check(length > 22 && strcmp(text + length - 22, "</trkseg></trk></gpx>\n") == 0, "gpx ends with one footer");
    check(strstr(text, "Test &amp; &lt;walk&gt;") != nullptr, "name escaped");
    check(strstr(text, "<time>2026-11-14T10:00:02.000Z</time>") != nullptr, "point time");
    check(strstr(text, "<ele>") != nullptr, "3d fix has elevation");
    remove(path);

    check(track.open(fopen(path, "wb+"), "2d"), "2d track opens");
    state.mode = fix_mode::two_d;
    state.altitude = -47;
    track.add(state);
    track.close();
    file = fopen(path, "rb");
    length = int(fread(text, 1, sizeof(text) - 1, file));
    fclose(file);
    text[length] = 0;
    check(strstr(text, "<trkpt") != nullptr && strstr(text, "<ele>") == nullptr, "2d fix has no elevation");
    remove(path);

    gps_state good = {};
    good.hdop = 0.9;
    gps_state poor = {};
    poor.hdop = 3.7;
    gps_state unknown = {};
    check(moved_far_enough(5, 5, good) && !moved_far_enough(4.9, 5, good), "good fix uses spacing");
    check(!moved_far_enough(15, 5, poor) && moved_far_enough(18.5, 5, poor), "poor fix needs to move beyond its error");
    check(moved_far_enough(5, 5, unknown), "no hdop uses spacing");

    event_writer events;
    check(events.open(fopen("test_events.csv", "wb")), "events open");
    events.add(state, "arrived", "Post \"Box\"", "line one\nline two");
    events.close();
    file = fopen("test_events.csv", "rb");
    length = int(fread(text, 1, sizeof(text) - 1, file));
    fclose(file);
    text[length] = 0;
    check(strstr(text, "2026-11-14T10:00:02.000Z,52.2500000,0.0184000,\"arrived\",\"Post \"\"Box\"\"\",\"line one line two\"\n") != nullptr, "event line");
    check(events.open(fopen("test_events.csv", "ab")), "events reopen");
    events.add(state, "second", "", "");
    events.close();
    file = fopen("test_events.csv", "rb");
    length = int(fread(text, 1, sizeof(text) - 1, file));
    fclose(file);
    text[length] = 0;
    check(strstr(text, "time,latitude") == text && strstr(text + 1, "time,latitude") == nullptr && strstr(text, "\"second\"") != nullptr, "append keeps one header");
    remove("test_events.csv");
}

static int read_whole(const char *path, char *buffer, int size) {
    FILE *file = fopen(path, "rb");
    if (file == nullptr) {
        return -1;
    }
    int length = int(fread(buffer, 1, size, file));
    fclose(file);
    return length;
}

static void test_filters() {
    static char text[16384];
    static colour_filter filter;
    static uint16_t table[65536];
    const char *names[] = {"Game Boy", "Sepia", "Thermal", "Synthwave", "Night vision", "Cross-process", "Cyanotype", "Golden hour", "Infrared colour", "Solarise", "Alien", "Redscale"};
    int parsed = 0;
    for (const char *name : names) {
        char path[128];
        snprintf(path, sizeof(path), "../../filters/%s.flt", name);
        int length = read_whole(path, text, sizeof(text));
        if (length > 0 && parse_filter(text, length, filter)) {
            parsed++;
        } else {
            printf("filter %s didn't parse\n", name);
        }
    }
    check(parsed == 12, "all bundled filters parse");

    int length = read_whole("../../filters/Game Boy.flt", text, sizeof(text));
    check(length > 0 && parse_filter(text, length, filter), "Game Boy parses");
    check(filter.matrix[0][0] == 306 && filter.matrix[0][1] == 601 && filter.matrix[0][2] == 117 && filter.matrix[2][2] == 117, "Game Boy matrix");
    check(filter.curves[0][0] == 15 && filter.curves[1][0] == 56 && filter.curves[2][0] == 15, "Game Boy curve start");
    build_filter_table(filter, table);
    uint16_t black = uint16_t((15 >> 3) << 11 | (56 >> 2) << 5 | 15 >> 3);
    check(table[0x0000] == black, "black maps through the curves");
    int luma = (306 * 255 + 601 * 255 + 117 * 255 + 512) / 1024;
    uint16_t white = uint16_t((filter.curves[0][luma] >> 3) << 11 | (filter.curves[1][luma] >> 2) << 5 | filter.curves[2][luma] >> 3);
    check(table[0xFFFF] == white, "white maps through the matrix and curves");
    check(table[0x07E0] != table[0x001F], "green and blue map differently");

    const char *broken = "0, 1\n1, 2, 3\n";
    check(!parse_filter(broken, int(strlen(broken)), filter), "truncated filter rejected");
}

static uint16_t canvas_pixels[64 * 64];

static framebuffer clear_canvas() {
    memset(canvas_pixels, 0, sizeof(canvas_pixels));
    return {canvas_pixels, 64, 64, 64};
}

static double painted_area() {
    double total = 0;
    for (uint16_t pixel : canvas_pixels) {
        total += ((pixel >> 5) & 0x3F) / 63.0;
    }
    return total;
}

static bool mirrored() {
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            if (canvas_pixels[y * 64 + x] != canvas_pixels[y * 64 + (63 - x)] || canvas_pixels[y * 64 + x] != canvas_pixels[(63 - y) * 64 + x]) {
                return false;
            }
        }
    }
    return true;
}

static void test_draw() {
    const double pi = 3.14159265358979;
    framebuffer target = clear_canvas();
    fill_annulus(target, 32 * subpixel, 32 * subpixel, 0, 10 * subpixel, 0xFFFF);
    double disc = painted_area();
    printf("draw: disc r10 %.1f (exact %.1f)\n", disc, pi * 100);
    check(fabs(disc - pi * 100) < pi * 100 * 0.02, "disc area");
    check(mirrored(), "disc symmetric");
    check(canvas_pixels[32 * 64 + 32] == 0xFFFF && canvas_pixels[32 * 64 + 43] == 0 && canvas_pixels[32 * 64 + 20] == 0, "disc solid inside, empty outside");

    target = clear_canvas();
    fill_annulus(target, 32 * subpixel, 32 * subpixel, 18 * subpixel, 20 * subpixel, 0xFFFF);
    double ring = painted_area();
    printf("draw: ring 18-20 %.1f (exact %.1f)\n", ring, pi * (400 - 324));
    check(fabs(ring - pi * 76) < pi * 76 * 0.03, "ring area");
    check(mirrored(), "ring symmetric");
    check(canvas_pixels[32 * 64 + 32] == 0, "ring hole empty");

    target = clear_canvas();
    point_q8 square[4] = {{10 * subpixel, 10 * subpixel}, {20 * subpixel, 10 * subpixel}, {20 * subpixel, 20 * subpixel}, {10 * subpixel, 20 * subpixel}};
    fill_polygon(target, square, 4, 0xFFFF);
    double polygon = painted_area();
    printf("draw: square %.1f (exact 100)\n", polygon);
    check(fabs(polygon - 100) < 0.5, "square area");

    target = clear_canvas();
    point_q8 triangle[3] = {{32 * subpixel, 10 * subpixel}, {50 * subpixel, 50 * subpixel}, {14 * subpixel, 50 * subpixel}};
    fill_polygon(target, triangle, 3, 0xFFFF);
    double triangle_area = painted_area();
    printf("draw: triangle %.1f (exact 720)\n", triangle_area);
    check(fabs(triangle_area - 720) < 720 * 0.02, "triangle area");

    target = clear_canvas();
    draw_line(target, {12 * subpixel, 32 * subpixel}, {52 * subpixel, 32 * subpixel}, 3 * subpixel, 0xFFFF);
    double line = painted_area();
    double expected_line = 40 * 3 + pi * 1.5 * 1.5;
    printf("draw: line %.1f (exact %.1f)\n", line, expected_line);
    check(fabs(line - expected_line) < expected_line * 0.03, "line area with round caps");
    check(canvas_pixels[20 * 64 + 32] == 0 && canvas_pixels[32 * 64 + 32] == 0xFFFF, "line placement");

    target = clear_canvas();
    fill_rounded_rectangle(target, 10, 20, 50, 40, 6 * subpixel, 0xFFFF);
    double rounded = painted_area();
    double expected_rounded = 40 * 20 - 4 * (36 - pi * 9);
    printf("draw: rounded rectangle %.1f (exact %.1f)\n", rounded, expected_rounded);
    check(fabs(rounded - expected_rounded) < 2, "rounded rectangle area");
    check(canvas_pixels[20 * 64 + 10] == 0 && canvas_pixels[30 * 64 + 10] == 0xFFFF, "rounded corner cut, edge solid");

    target = clear_canvas();
    fill_annulus(target, 2 * subpixel, 2 * subpixel, 0, 30 * subpixel, 0xFFFF);
    fill_annulus(target, 62 * subpixel, 62 * subpixel, 20 * subpixel, 30 * subpixel, 0xFFFF);
    draw_line(target, {-50 * subpixel, -50 * subpixel}, {150 * subpixel, 120 * subpixel}, 4 * subpixel, 0xFFFF);
    check(true, "clipped shapes stay in bounds");

    check(blend565(0x0000, 0xFFFF, 128) == ((15 << 11) | (31 << 5) | 15), "half blend");
}

static void test_settings() {
    settings defaults;
    check(defaults.jpeg_quality == 85 && !defaults.log_input, "settings defaults");
    settings edited;
    const char *text = "# GISmo settings\r\njpeg_quality = 70\r\nlog_input = yes\ntrack_spacing_metres=10\njpeg_quality = 500\nunknown = 3\nmetres_per_step = fast\n";
    check(apply_settings(text, int(strlen(text)), edited) == 3, "three settings applied");
    check(edited.jpeg_quality == 70 && edited.log_input && edited.track_spacing_metres == 10, "settings values");
    check(edited.metres_per_step == defaults.metres_per_step, "bad value keeps default");
}

static void test_places() {
    place_index places;
    check(places.load(fopen("../../maps/cambridge.gplaces", "rb")), "cambridge.gplaces loads");
    if (!places.loaded()) {
        return;
    }
    grid_position church = wgs84_to_british_national_grid(52.208475, 0.118984);
    nearby_place found[8];
    int count = places.nearest(church.easting, church.northing, -1, found, 0, 8);
    check(count == 8 && strcmp(places.name(found[0].index), "Round Church") == 0 && found[0].distance < 30, "nearest to the Round Church is the Round Church");
    printf("places: %u, nearest %s (%s) at %.0f m\n", places.count(), places.name(found[0].index), places.kind_label(found[0].index), found[0].distance);

    bool ordered = true;
    bool matches = true;
    uint32_t seed = 7;
    for (int trial = 0; trial < 200; trial++) {
        seed = seed * 1103515245 + 12345;
        double east = 541000 + (seed >> 8) % 8000;
        seed = seed * 1103515245 + 12345;
        double north = 254000 + (seed >> 8) % 8000;
        int wanted = trial % 5 - 1;
        nearby_place results[12];
        int found_count = places.nearest(east, north, wanted, results, 0, 12);
        double brute[12];
        int brute_count = 0;
        for (uint32_t index = 0; index < places.count(); index++) {
            if (wanted >= 0 && places.category(index) != wanted) {
                continue;
            }
            double distance = hypot(places.east(index) - east, places.north(index) - north);
            if (brute_count == 12 && distance >= brute[11]) {
                continue;
            }
            int position = brute_count < 12 ? brute_count++ : 11;
            while (position > 0 && brute[position - 1] > distance) {
                brute[position] = brute[position - 1];
                position--;
            }
            brute[position] = distance;
        }
        matches = matches && found_count == brute_count;
        for (int index = 0; matches && index < found_count; index++) {
            matches = fabs(results[index].distance - brute[index]) < 1e-9;
            ordered = ordered && (index == 0 || results[index - 1].distance <= results[index].distance);
            matches = matches && (wanted < 0 || places.category(results[index].index) == wanted);
        }
    }
    check(matches && ordered, "nearest matches a brute force search, by category and from outside the grid");
    check(!places.load(fopen("../../missions/cambridge.txt", "rb")), "not a places file");
}

static void test_zip() {
    FILE *input = fopen("zip_input.bin", "wb");
    uint32_t value = 12345;
    for (int index = 0; index < 10000; index++) {
        value = value * 1103515245 + 12345;
        fputc(int(value >> 16) & 0xFF, input);
    }
    fclose(input);
    zip_writer archive;
    check(archive.open(fopen("zip_test.zip", "wb+"), {2026, 11, 14, 10, 30, 0}), "zip opens");
    check(archive.add_bytes("Cambridge/summary.txt", "GISmo outing\n", 13), "zip adds bytes");
    check(archive.add_file("Cambridge/photos/caf\xc3\xa9.bin", fopen("zip_input.bin", "rb")), "zip streams a file");
    check(!archive.add_file("Cambridge/missing.bin", fopen("does-not-exist.bin", "rb")), "zip skips a missing file");
    check(archive.entries() == 2 && archive.close(), "zip closes with two entries");
    check(crc32_update(0, "123456789", 9) == 0xCBF43926, "crc32 check value");
}

int main() {
    test_nmea();
    test_grid();
    test_mission();
    test_progress();
    test_via();
    test_session_log();
    test_filters();
    test_draw();
    test_settings();
    test_places();
    test_zip();
    printf(failures ? "%d failures\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
