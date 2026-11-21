#pragma once

#include <stdint.h>
#include <stdio.h>

namespace gismo {

constexpr int max_zip_entries = 512;
constexpr int max_zip_name = 96;

struct zip_time {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
};

class zip_writer {
public:
    zip_writer();
    ~zip_writer();

    bool open(FILE *file, const zip_time &time);
    bool add_bytes(const char *name, const void *data, uint32_t length);
    bool add_file(const char *name, FILE *source);
    bool close();

    int entries() const {
        return entry_count;
    }

private:
    struct entry {
        char name[max_zip_name];
        uint32_t crc;
        uint32_t size;
        uint32_t offset;
    };

    bool begin_entry(const char *name);
    bool finish_entry(uint32_t crc, uint32_t size);

    FILE *file;
    uint16_t dos_time;
    uint16_t dos_date;
    entry entry_list[max_zip_entries];
    int entry_count;
    bool failed;
};

uint32_t crc32_update(uint32_t crc, const void *data, uint32_t length);

}
