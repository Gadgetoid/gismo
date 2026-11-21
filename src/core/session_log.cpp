#include "session_log.h"

#include <string.h>

namespace gismo {

namespace {

const char gpx_footer[] = "</trkseg></trk></gpx>\n";

void write_escaped(FILE *file, const char *text) {
    for (const char *character = text; *character; character++) {
        switch (*character) {
        case '&':
            fputs("&amp;", file);
            break;
        case '<':
            fputs("&lt;", file);
            break;
        case '>':
            fputs("&gt;", file);
            break;
        case '"':
            fputs("&quot;", file);
            break;
        default:
            fputc(*character, file);
        }
    }
}

void write_csv_field(FILE *file, const char *text) {
    fputc('"', file);
    for (const char *character = text; *character; character++) {
        if (*character == '"') {
            fputc('"', file);
        }
        fputc(*character == '\n' ? ' ' : *character, file);
    }
    fputc('"', file);
}

}

void format_utc(const utc_time &time, char *text, int text_size) {
    snprintf(text, text_size, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", time.year, time.month, time.day, time.hour, time.minute, time.second, time.millisecond);
}

track_writer::track_writer() : file(nullptr), point_count(0) {
}

track_writer::~track_writer() {
    close();
}

bool track_writer::open(FILE *opened, const char *name) {
    close();
    file = opened;
    if (file == nullptr) {
        return false;
    }
    fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", file);
    fputs("<gpx version=\"1.1\" creator=\"GISmo\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n", file);
    fputs("<trk><name>", file);
    write_escaped(file, name);
    fputs("</name><trkseg>\n", file);
    write_footer();
    return true;
}

void track_writer::write_footer() {
    fputs(gpx_footer, file);
    fflush(file);
    fseek(file, -long(sizeof(gpx_footer) - 1), SEEK_CUR);
}

void track_writer::add(const gps_state &state) {
    if (file == nullptr || !state.has_position) {
        return;
    }
    fprintf(file, "<trkpt lat=\"%.7f\" lon=\"%.7f\">", state.latitude, state.longitude);
    if (state.mode == fix_mode::three_d) {
        fprintf(file, "<ele>%.1f</ele>", state.altitude);
    }
    if (state.has_time && state.has_date) {
        char time[32];
        format_utc(state.time, time, sizeof(time));
        fprintf(file, "<time>%s</time>", time);
    }
    fprintf(file, "<fix>%s</fix><sat>%d</sat>", state.mode == fix_mode::three_d ? "3d" : "2d", state.satellites_used);
    if (state.hdop > 0) {
        fprintf(file, "<hdop>%.1f</hdop>", state.hdop);
    }
    fputs("</trkpt>\n", file);
    point_count++;
    write_footer();
}

void track_writer::close() {
    if (file) {
        fclose(file);
        file = nullptr;
    }
}

event_writer::event_writer() : file(nullptr) {
}

event_writer::~event_writer() {
    close();
}

bool event_writer::open(FILE *opened) {
    close();
    file = opened;
    if (file == nullptr) {
        return false;
    }
    fseek(file, 0, SEEK_END);
    if (ftell(file) == 0) {
        fputs("time,latitude,longitude,event,waypoint,detail\n", file);
    }
    fflush(file);
    return true;
}

void event_writer::add(const gps_state &state, const char *event, const char *waypoint, const char *detail) {
    if (file == nullptr) {
        return;
    }
    char time[32] = "";
    if (state.has_time && state.has_date) {
        format_utc(state.time, time, sizeof(time));
    }
    fprintf(file, "%s,", time);
    if (state.has_position) {
        fprintf(file, "%.7f,%.7f,", state.latitude, state.longitude);
    } else {
        fputs(",,", file);
    }
    write_csv_field(file, event);
    fputc(',', file);
    write_csv_field(file, waypoint);
    fputc(',', file);
    write_csv_field(file, detail);
    fputc('\n', file);
    fflush(file);
}

void event_writer::close() {
    if (file) {
        fclose(file);
        file = nullptr;
    }
}

double position_error_metres(const gps_state &state) {
    return state.hdop > 0 ? state.hdop * metres_per_hdop : 0;
}

bool moved_far_enough(double moved_metres, double spacing_metres, const gps_state &state) {
    double error = position_error_metres(state);
    return moved_metres >= (error > spacing_metres ? error : spacing_metres);
}

}
