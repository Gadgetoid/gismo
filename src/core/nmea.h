#pragma once

#include <stdint.h>

namespace gismo {

constexpr int max_satellites = 32;

enum class fix_mode : uint8_t {
    none = 1,
    two_d = 2,
    three_d = 3,
};

struct satellite {
    int prn;
    int elevation;
    int azimuth;
    int snr;
    bool used;
};

struct utc_time {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int millisecond;
};

struct gps_state {
    bool has_position;
    double latitude;
    double longitude;
    double altitude;
    double speed_metres_per_second;
    double course_degrees;
    bool has_course;
    int fix_quality;
    fix_mode mode;
    int satellites_used;
    double pdop;
    double hdop;
    double vdop;
    bool has_time;
    bool has_date;
    utc_time time;
    int satellite_count;
    satellite satellites[max_satellites];
    uint32_t position_updates;
    uint32_t satellite_updates;
    uint32_t sentences;
    uint32_t checksum_errors;
};

class nmea_parser {
public:
    nmea_parser();

    void feed(const char *data, int length);
    bool parse_sentence(const char *sentence);

    const gps_state &state() const {
        return current;
    }

private:
    void parse_gga(char **fields, int count);
    void parse_rmc(char **fields, int count);
    void parse_gsa(char **fields, int count);
    void parse_gsv(char **fields, int count);
    void mark_used_satellites();

    gps_state current;
    char line[100];
    int line_length;
    bool line_overflow;

    satellite pending[max_satellites];
    int pending_count;
    int pending_expected_message;
    int used_prns[12];
    int used_prn_count;
};

}
