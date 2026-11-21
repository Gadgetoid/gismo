#include "nmea.h"

#include <string.h>

namespace gismo {

namespace {

constexpr int max_fields = 24;
constexpr double knots_to_metres_per_second = 1852.0 / 3600.0;

int hex_value(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    return -1;
}

bool is_empty(const char *field) {
    return field[0] == 0;
}

int parse_int(const char *field) {
    int value = 0;
    bool negative = *field == '-';
    if (negative) {
        field++;
    }
    while (*field >= '0' && *field <= '9') {
        value = value * 10 + (*field - '0');
        field++;
    }
    return negative ? -value : value;
}

double parse_decimal(const char *field) {
    bool negative = *field == '-';
    if (negative) {
        field++;
    }
    double value = 0;
    while (*field >= '0' && *field <= '9') {
        value = value * 10 + (*field - '0');
        field++;
    }
    if (*field == '.') {
        field++;
        double scale = 0.1;
        while (*field >= '0' && *field <= '9') {
            value += (*field - '0') * scale;
            scale *= 0.1;
            field++;
        }
    }
    return negative ? -value : value;
}

int parse_two_digits(const char *text) {
    return (text[0] - '0') * 10 + (text[1] - '0');
}

bool parse_coordinate(const char *value, const char *hemisphere, int degree_digits, double &result) {
    if (is_empty(value) || is_empty(hemisphere) || strlen(value) < size_t(degree_digits + 2)) {
        return false;
    }
    int degrees = 0;
    for (int index = 0; index < degree_digits; index++) {
        degrees = degrees * 10 + (value[index] - '0');
    }
    double minutes = parse_decimal(value + degree_digits);
    result = degrees + minutes / 60.0;
    if (hemisphere[0] == 'S' || hemisphere[0] == 'W') {
        result = -result;
    }
    return true;
}

bool parse_time(const char *field, utc_time &time) {
    if (strlen(field) < 6) {
        return false;
    }
    time.hour = parse_two_digits(field);
    time.minute = parse_two_digits(field + 2);
    time.second = parse_two_digits(field + 4);
    time.millisecond = field[6] == '.' ? int(parse_decimal(field + 6) * 1000 + 0.5) : 0;
    return true;
}

}

nmea_parser::nmea_parser() {
    memset(&current, 0, sizeof(current));
    current.mode = fix_mode::none;
    line_length = 0;
    line_overflow = false;
    pending_count = 0;
    pending_expected_message = 1;
    used_prn_count = 0;
}

void nmea_parser::feed(const char *data, int length) {
    for (int index = 0; index < length; index++) {
        char character = data[index];
        if (character == '$') {
            line_length = 0;
            line_overflow = false;
        }
        if (character == '\r' || character == '\n') {
            if (line_length > 0 && !line_overflow) {
                line[line_length] = 0;
                parse_sentence(line);
            }
            line_length = 0;
            line_overflow = false;
            continue;
        }
        if (line_length < int(sizeof(line)) - 1) {
            line[line_length++] = character;
        } else {
            line_overflow = true;
        }
    }
}

bool nmea_parser::parse_sentence(const char *sentence) {
    if (sentence[0] != '$') {
        return false;
    }
    char buffer[100];
    size_t length = strlen(sentence);
    if (length >= sizeof(buffer)) {
        return false;
    }
    memcpy(buffer, sentence, length + 1);

    char *star = strchr(buffer, '*');
    if (star == nullptr || hex_value(star[1]) < 0 || hex_value(star[2]) < 0) {
        current.checksum_errors++;
        return false;
    }
    int checksum = 0;
    for (char *character = buffer + 1; character < star; character++) {
        checksum ^= uint8_t(*character);
    }
    if (checksum != hex_value(star[1]) * 16 + hex_value(star[2])) {
        current.checksum_errors++;
        return false;
    }
    *star = 0;

    char *fields[max_fields];
    int count = 0;
    char *field = buffer + 1;
    while (count < max_fields) {
        fields[count++] = field;
        char *comma = strchr(field, ',');
        if (comma == nullptr) {
            break;
        }
        *comma = 0;
        field = comma + 1;
    }
    if (strlen(fields[0]) != 5) {
        return false;
    }
    current.sentences++;

    const char *type = fields[0] + 2;
    if (strcmp(type, "GGA") == 0) {
        parse_gga(fields, count);
    } else if (strcmp(type, "RMC") == 0) {
        parse_rmc(fields, count);
    } else if (strcmp(type, "GSA") == 0) {
        parse_gsa(fields, count);
    } else if (strcmp(type, "GSV") == 0) {
        parse_gsv(fields, count);
    } else {
        return false;
    }
    return true;
}

void nmea_parser::parse_gga(char **fields, int count) {
    if (count < 10) {
        return;
    }
    if (parse_time(fields[1], current.time)) {
        current.has_time = true;
    }
    current.fix_quality = parse_int(fields[6]);
    current.satellites_used = parse_int(fields[7]);
    if (!is_empty(fields[8])) {
        current.hdop = parse_decimal(fields[8]);
    }
    double latitude;
    double longitude;
    if (current.fix_quality > 0 && parse_coordinate(fields[2], fields[3], 2, latitude) && parse_coordinate(fields[4], fields[5], 3, longitude)) {
        current.latitude = latitude;
        current.longitude = longitude;
        if (!is_empty(fields[9])) {
            current.altitude = parse_decimal(fields[9]);
        }
        current.has_position = true;
        current.position_updates++;
    } else {
        current.has_position = false;
    }
}

void nmea_parser::parse_rmc(char **fields, int count) {
    if (count < 10) {
        return;
    }
    if (parse_time(fields[1], current.time)) {
        current.has_time = true;
    }
    if (strlen(fields[9]) == 6) {
        current.time.day = parse_two_digits(fields[9]);
        current.time.month = parse_two_digits(fields[9] + 2);
        current.time.year = 2000 + parse_two_digits(fields[9] + 4);
        current.has_date = true;
    }
    if (fields[2][0] != 'A') {
        current.has_position = false;
        return;
    }
    double latitude;
    double longitude;
    if (!parse_coordinate(fields[3], fields[4], 2, latitude) || !parse_coordinate(fields[5], fields[6], 3, longitude)) {
        return;
    }
    current.latitude = latitude;
    current.longitude = longitude;
    current.speed_metres_per_second = is_empty(fields[7]) ? 0 : parse_decimal(fields[7]) * knots_to_metres_per_second;
    current.has_course = !is_empty(fields[8]);
    if (current.has_course) {
        current.course_degrees = parse_decimal(fields[8]);
    }
    current.has_position = true;
    current.position_updates++;
}

void nmea_parser::parse_gsa(char **fields, int count) {
    if (count < 18) {
        return;
    }
    int mode = parse_int(fields[2]);
    current.mode = mode == 3 ? fix_mode::three_d : mode == 2 ? fix_mode::two_d : fix_mode::none;
    used_prn_count = 0;
    for (int index = 3; index < 15; index++) {
        if (!is_empty(fields[index])) {
            used_prns[used_prn_count++] = parse_int(fields[index]);
        }
    }
    current.pdop = parse_decimal(fields[15]);
    current.hdop = parse_decimal(fields[16]);
    current.vdop = parse_decimal(fields[17]);
    mark_used_satellites();
}

void nmea_parser::parse_gsv(char **fields, int count) {
    if (count < 4) {
        return;
    }
    int total_messages = parse_int(fields[1]);
    int message = parse_int(fields[2]);
    if (message == 1) {
        pending_count = 0;
        pending_expected_message = 1;
    }
    if (message != pending_expected_message) {
        return;
    }
    pending_expected_message++;

    for (int index = 4; index < count; index += 4) {
        if (is_empty(fields[index]) || pending_count >= max_satellites) {
            continue;
        }
        satellite &entry = pending[pending_count++];
        entry.prn = parse_int(fields[index]);
        entry.elevation = index + 1 < count ? parse_int(fields[index + 1]) : 0;
        entry.azimuth = index + 2 < count ? parse_int(fields[index + 2]) : 0;
        entry.snr = index + 3 < count && !is_empty(fields[index + 3]) ? parse_int(fields[index + 3]) : 0;
        entry.used = false;
    }

    if (message == total_messages) {
        memcpy(current.satellites, pending, sizeof(satellite) * pending_count);
        current.satellite_count = pending_count;
        mark_used_satellites();
        current.satellite_updates++;
        pending_expected_message = 1;
    }
}

void nmea_parser::mark_used_satellites() {
    for (int index = 0; index < current.satellite_count; index++) {
        satellite &entry = current.satellites[index];
        entry.used = false;
        for (int used = 0; used < used_prn_count; used++) {
            if (used_prns[used] == entry.prn) {
                entry.used = true;
            }
        }
    }
}

}
