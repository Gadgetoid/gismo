#pragma once

#include <windows.h>

#include <stdint.h>

#include "camera.h"
#include "gps_port.h"
#include "grid.h"
#include "map_pack.h"
#include "mission.h"
#include "places.h"
#include "route.h"
#include "session_log.h"
#include "settings.h"

namespace gismo {

struct icon;

constexpr UINT camera_message = WM_APP + 2;
constexpr UINT_PTR keep_awake_timer = 1;
constexpr UINT_PTR pan_mode_timer = 2;

constexpr int thumb_width = 80;
constexpr int thumb_height = 60;

constexpr int max_packs = 6;
constexpr int max_missions = 16;
constexpr int max_photos = 200;
constexpr int max_filters = 24;
constexpr int max_masks = 24;
constexpr int max_trail = 4096;
constexpr int max_logged_drags = 3;
constexpr int max_place_files = 4;
constexpr int max_nearby = 40;
constexpr int max_visited = 64;

inline constexpr double zoom_levels[] = {0.5, 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024};
constexpr int zoom_count = sizeof(zoom_levels) / sizeof(zoom_levels[0]);

struct layout_metrics {
    int bar_height;
    int button_height;
    int strip_height;
    int floating_radius;
    int list_row_height;
    int list_text_gap;
    int thumb_row_height;
    int clue_box_height;
    int preview_height;
    int shutter_radius;
};

inline constexpr layout_metrics layout = {
    .bar_height = 26,
    .button_height = 40,
    .strip_height = 36,
    .floating_radius = 19,
    .list_row_height = 34,
    .list_text_gap = 8,
    .thumb_row_height = thumb_height + 2,
    .clue_box_height = 50,
    .preview_height = 120,
    .shutter_radius = 26,
};

inline DWORD milliseconds(double seconds) {
    return DWORD(seconds * 1000);
}

enum class screen {
    map,
    sky,
    list,
    photos,
    missions,
    clue,
    task,
    summary,
    camera,
    photo_view,
    hint_view,
    found,
};

inline constexpr screen pages[] = {screen::map, screen::list, screen::photos, screen::sky};
constexpr int page_count = sizeof(pages) / sizeof(pages[0]);

enum class button {
    none,
    page_map,
    page_list,
    page_photos,
    page_sky,
    follow,
    layer,
    route,
    close,
};

struct loaded_pack {
    map_pack pack;
    map_tile_cache *cache;
    bool overview;
    double finest;
    double coarsest;
    wchar_t name[64];
};

struct filter_entry {
    wchar_t name[48];
    wchar_t path[MAX_PATH];
};

struct mission_entry {
    wchar_t path[MAX_PATH];
    wchar_t stem[64];
    char title[64];
    int count;
    int done;
    bool valid;
    int points;
    double east[max_waypoints];
    double north[max_waypoints];
    bool point_done[max_waypoints];
};

struct coverage_mask {
    int32_t inner;
    int32_t outer;
    int size;
    uint8_t *alpha;
    uint32_t last_used;
};

struct task_layout {
    RECT banner;
    RECT name;
    RECT prompt;
    HFONT prompt_font;
    RECT feedback;
    RECT choices[max_choices];
    RECT minus;
    RECT plus;
    RECT value;
    RECT action;
};

struct outing_stats {
    double metres;
    DWORD seconds;
    int speed_tenths;
    int stops_done;
    int stops_total;
    int photos;
};

struct grid_point {
    double east;
    double north;
};

struct ui_state {
    HWND window;
    HDC memory_dc;
    HBITMAP bitmap;
    HBITMAP previous_bitmap;
    uint16_t *bits;
    int width;
    int height;
    int stride;
    HFONT small_font;
    HFONT medium_font;
    HFONT large_font;
    HFONT huge_font;
    coverage_mask masks[max_masks];
    uint32_t mask_clock;
    screen current;
};

struct map_state {
    loaded_pack packs[max_packs];
    int pack_count;
    int overview_pack;
    int detail_packs[max_packs];
    int detail_count;
    int layer;
    int zoom;
    double centre_east;
    double centre_north;
    bool follow;
    bool pan_mode;
    DWORD pan_mode_tick;
    grid_point trail[max_trail];
    int trail_count;
    route_graph routes;
    bool routes_tried;
    int routes_tried_target;
    bool route_hint;
    bool route_valid;
    int route_target;
    double route_origin_east;
    double route_origin_north;
    uint32_t *route_nodes;
    route_result route;
    int64_t route_goal_node;
};

struct mission_state {
    mission quest;
    bool have_mission;
    wchar_t mission_error[160];
    mission_entry missions[max_missions];
    int mission_count;
    int mission_index;
    int picker_selected;
    int picker_scroll;
    int target;
    int task_waypoint;
    int count_value;
    int choice_feedback;
    int choice_selected;
    int list_scroll;
    int list_selected;
    bool list_dragging;
    int list_drag_start_y;
    int list_drag_start_scroll;
    bool list_dragged;
    double warmth_distance;
    DWORD warmth_tick;
    int warmth;
};

struct location_state {
    gps_port gps;
    gps_state gps_now;
    bool gps_connected;
    grid_position position;
};

struct outing_state {
    track_writer track;
    event_writer events;
    wchar_t session_folder[MAX_PATH];
    bool session_started;
    bool run_open;
    bool clock_set;
    double distance_walked;
    SYSTEMTIME started_time;
    wchar_t saved_note[96];
    DWORD last_track_tick;
    DWORD started_tick;
    HANDLE backlight;
};

struct input_state {
    int logged_keys;
    bool windows_alone;
    bool dragging;
    POINT drag_start;
    double drag_east;
    double drag_north;
    bool dragged;
    POINT drag_samples[3];
    int drag_sample_index;
    POINT drag_last_raw;
    int logged_drags;
    int logged_drag_samples;
};

struct canvas_state {
    HDC dc;
    HBITMAP bitmap;
    uint16_t *bits;
    int stride;
};

struct camera_state {
    camera device;
    uint16_t *frame;
    uint16_t *photo_pixels;
    uint8_t *jpeg_buffer;
    uint16_t *filter_table;
    filter_entry filters[max_filters];
    int filter_count;
    int filter_index;
    DWORD flash_tick;
    int camera_task;
    wchar_t camera_note[96];
    int photos_taken;
};

struct photos_state {
    wchar_t photo_names[max_photos][40];
    int photo_count;
    bool photos_loaded;
    uint16_t *thumbs;
    int photo_scroll;
    bool photo_dragging;
    bool photo_dragged;
    int photo_drag_start_y;
    int photo_drag_start_scroll;
    int viewing;
    uint16_t *view_pixels;
    bool view_loaded;
    bool hint_loaded;
    bool hint_found;
};

struct explore_state {
    place_index files[max_place_files];
    int file_count;
    bool files_tried;
    int category;
    nearby_place nearby[max_nearby];
    int nearby_count;
    bool nearby_valid;
    double nearby_east;
    double nearby_north;
    int nearby_category;
    bool nearby_from_gps;
    bool active;
    waypoint spot;
    int spot_category;
    int serial;
    const place_index *visited_source[max_visited];
    uint32_t visited_index[max_visited];
    int visited_count;
};

struct application {
    gismo::settings settings;
    ui_state ui;
    map_state map;
    mission_state mission;
    location_state location;
    outing_state outing;
    input_state input;
    canvas_state canvas;
    camera_state camera;
    photos_state photos;
    explore_state explore;
};

extern application app;

LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

void exe_folder(wchar_t *folder);
void utf8_to_wide(const char *text, wchar_t *output, int output_size);
bool folder_exists(const wchar_t *path);
void find_data_folder(const wchar_t *name, wchar_t *folder);
void data_root(wchar_t *folder);
int read_file(const wchar_t *path, char *buffer, int size);
void load_settings();
bool write_whole_file(const wchar_t *path, const void *data, DWORD length);

void sessions_folder(wchar_t *folder);
void mission_folder(const wchar_t *stem, wchar_t *folder);
void apply_saved_progress(const wchar_t *stem, mission &target);
void scan_missions();
void remember_mission();
int remembered_mission();
void save_progress();
void close_run();
void open_run();
void start_session();
void set_clock_from_gps();
int next_target(int from);
int completed_count();
void select_mission(int index);
void update_warmth(double distance);
void open_task(int index);
void show_clue();
void finish_task(const char *detail);
void mark_done(waypoint &point, const char *detail);
void advance_target(int from);
void pass_via(int index);
void record_position();
void check_arrival();
void on_gps_update();
void set_target(int index);
const waypoint *current_target();
int target_key();
void tap_target_strip();

void create_surface();
HFONT make_font(int height, bool bold);
void fill(const RECT &area, COLORREF colour);
void text(const wchar_t *value, RECT area, COLORREF colour, HFONT font, UINT format);
void text_utf8(const char *value, RECT area, COLORREF colour, HFONT font, UINT format);
RECT measure_text(const wchar_t *value, RECT area, HFONT font, UINT format);
RECT measure_text_utf8(const char *value, RECT area, HFONT font, UINT format);
uint16_t to_565(COLORREF colour);
void draw_icon_into(uint16_t *bits, int stride, int width, int height, const icon &glyph, int centre_x, int centre_y, COLORREF colour);
void draw_icon(const icon &glyph, int centre_x, int centre_y, COLORREF colour);
const coverage_mask *annulus_mask(int32_t inner, int32_t outer);
void stamp_mask(framebuffer &target, const coverage_mask &mask, int centre_x, int centre_y, uint16_t colour);
void draw_annulus(framebuffer &target, int x, int y, int32_t inner, int32_t outer, uint16_t colour);
void circle_into(framebuffer &target, int x, int y, int radius, COLORREF outline, int thickness, COLORREF fill_colour, bool filled);
void circle(int x, int y, int radius, COLORREF outline, int thickness, COLORREF fill_colour, bool filled);
void rounded_button(const RECT &area, const wchar_t *label, COLORREF background, HFONT font);
void arrow(int centre_x, int centre_y, int size, double degrees, COLORREF colour);
void format_distance(double metres, wchar_t *output);
RECT floating_rect(int centre_x, int centre_y);
RECT button_rect(button which);
button hit_button(int x, int y);
void draw_toolbar();
void draw_floating_button(button which, const icon &glyph, bool active);
void draw_status_bar();
void render();
void redraw();
void on_paint(HWND window);

double view_metres_per_pixel();
map_view current_view();
framebuffer current_framebuffer();
void load_maps();
bool draw_layers_into(framebuffer target, const map_view &view);
void draw_layers();
bool route_tile_core(const wchar_t *name, double &west, double &south, double &size);
int64_t nearest_route_node(double east, double north);
bool try_route_file(const wchar_t *folder, const wchar_t *name, const waypoint *goal);
bool load_routes();
bool route_shown();
void update_route();
double route_distance_to_target();
void draw_route();
void draw_trail();
void draw_explore_spot();
void draw_waypoints();
void draw_position();
void draw_target_strip();
void draw_pan_badge();
void set_pan_mode(bool on);
void check_pan_mode();
void draw_map_screen();
void show_map_if_overlay();
void zoom_by(int steps);
void pan_by(int columns, int rows);

void draw_sky_screen();

int list_top();
int visible_list_rows();
int max_list_scroll();
void keep_selection_visible();
RECT mission_header_rect();
RECT header_summary_rect();
void draw_mission_header(const wchar_t *fallback);
void draw_list_screen();
int picker_rows();
int picker_top();
int visible_picker_rows();
void draw_mission_preview(int top);
void keep_picker_visible();
void draw_missions_screen();
RECT task_button_rect(int index, int count);
const char *task_prompt(const waypoint &point);
bool choice_labels_fit(const waypoint &point, int width);
task_layout layout_task_attempt(const waypoint &point, bool compact, bool columns, bool small_prompt);
task_layout layout_task(const waypoint &point);
void draw_task_screen();
bool has_photo_hint();
void photo_hint_path(wchar_t *path);
void open_hint();
void draw_hint_view_screen();
void draw_clue_screen();
void open_picker();
void choose_picker_row(int index);
void tap_task(int x, int y);
void choose_list_row(int index);
void tap_list(int x, int y);
int list_count();
void key_task(int key);

bool create_canvas();
void rotate_canvas_to_screen();
void screen_to_canvas(int x, int y, int &canvas_x, int &canvas_y);
void canvas_text(const wchar_t *value, RECT area, HFONT font, UINT format);
void canvas_circle(int x, int y, int radius, COLORREF outline, COLORREF fill_colour);
RECT canvas_close_rect();
void fill_canvas(RECT area, COLORREF colour);

RECT shutter_rect();
const wchar_t *filter_name();
void apply_filter(const uint16_t *source, uint16_t *target, int target_stride);
const wchar_t *filter_name_at(int index);
RECT filter_wheel_rect();
void draw_filter_wheel();
void draw_camera_screen();
void load_filters();
void select_filter(int index);
void step_filter(int direction);
void open_camera(int task);
void close_camera(screen next);
void photo_name(wchar_t *name);
void take_photo();
void key_camera_screen(int key);
void tap_camera_screen(int x, int y);

void photo_folder(wchar_t *folder, const wchar_t *subfolder);
void load_photos();
int photo_grid_top();
int photo_columns();
int visible_photo_rows();
int max_photo_scroll();
RECT photo_camera_rect();
void draw_photos_screen();
void draw_photo_view_screen();
void open_photo(int index);
void tap_photo_view(int x, int y);

void show_page(screen page);
void step_page(int direction);
void press(button which);
void on_tap(HWND window, int x, int y);
int median_of_three(int a, int b, int c);
bool smoothed_drag_point(int x, int y, POINT &smoothed);
bool is_select_key(int key);
bool on_button_key(int key);
void on_key(int key);

void load_places();
void refresh_nearby(bool force);
COLORREF category_colour(int category);
const wchar_t *compass_point(double degrees);
bool place_visited(const place_index *source, uint32_t index);
void draw_nearby_screen();
void tap_nearby(int x, int y);
void key_nearby(int key);
void choose_nearby(int index);
void clear_explore_target();
void open_found();
void draw_found_screen();

outing_stats current_outing_stats();
void format_duration(DWORD seconds, wchar_t *output);
void format_speed(const outing_stats &stats, wchar_t *output);
bool mission_finished();
void open_summary();
bool save_outing();
void draw_summary_screen();
void tap_summary(int x, int y);

}
