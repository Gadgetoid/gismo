#include "zip_writer.h"

#include <string.h>

namespace gismo {

namespace {

constexpr uint32_t local_signature = 0x04034B50;
constexpr uint32_t central_signature = 0x02014B50;
constexpr uint32_t end_signature = 0x06054B50;
constexpr uint16_t version = 20;
constexpr uint16_t utf8_names = 0x0800;
constexpr int local_header_size = 30;
constexpr int copy_chunk = 4096;

void put16(uint8_t *bytes, uint16_t value) {
    bytes[0] = uint8_t(value);
    bytes[1] = uint8_t(value >> 8);
}

void put32(uint8_t *bytes, uint32_t value) {
    put16(bytes, uint16_t(value));
    put16(bytes + 2, uint16_t(value >> 16));
}

}

uint32_t crc32_update(uint32_t crc, const void *data, uint32_t length) {
    static uint32_t table[256];
    if (table[1] == 0) {
        for (uint32_t index = 0; index < 256; index++) {
            uint32_t value = index;
            for (int bit = 0; bit < 8; bit++) {
                value = value & 1 ? 0xEDB88320 ^ (value >> 1) : value >> 1;
            }
            table[index] = value;
        }
    }
    const uint8_t *bytes = static_cast<const uint8_t *>(data);
    crc = ~crc;
    for (uint32_t index = 0; index < length; index++) {
        crc = table[(crc ^ bytes[index]) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

zip_writer::zip_writer() : file(nullptr), dos_time(0), dos_date(0), entry_count(0), failed(false) {
}

zip_writer::~zip_writer() {
    if (file) {
        fclose(file);
    }
}

bool zip_writer::open(FILE *target, const zip_time &time) {
    file = target;
    entry_count = 0;
    failed = file == nullptr;
    dos_time = uint16_t(time.hour << 11 | time.minute << 5 | time.second / 2);
    dos_date = uint16_t((time.year - 1980) << 9 | time.month << 5 | time.day);
    return !failed;
}

bool zip_writer::begin_entry(const char *name) {
    if (failed || entry_count >= max_zip_entries || strlen(name) >= size_t(max_zip_name)) {
        failed = true;
        return false;
    }
    entry &current = entry_list[entry_count];
    strcpy(current.name, name);
    current.offset = uint32_t(ftell(file));
    uint8_t header[local_header_size] = {};
    uint16_t name_length = uint16_t(strlen(name));
    put32(header, local_signature);
    put16(header + 4, version);
    put16(header + 6, utf8_names);
    put16(header + 10, dos_time);
    put16(header + 12, dos_date);
    put16(header + 26, name_length);
    failed = fwrite(header, 1, sizeof(header), file) != sizeof(header) || fwrite(name, 1, name_length, file) != name_length;
    return !failed;
}

bool zip_writer::finish_entry(uint32_t crc, uint32_t size) {
    entry &current = entry_list[entry_count];
    current.crc = crc;
    current.size = size;
    long end = ftell(file);
    uint8_t sizes[12];
    put32(sizes, crc);
    put32(sizes + 4, size);
    put32(sizes + 8, size);
    failed = failed || fseek(file, long(current.offset) + 14, SEEK_SET) != 0 || fwrite(sizes, 1, sizeof(sizes), file) != sizeof(sizes) || fseek(file, end, SEEK_SET) != 0;
    if (!failed) {
        entry_count++;
    }
    return !failed;
}

bool zip_writer::add_bytes(const char *name, const void *data, uint32_t length) {
    if (!begin_entry(name)) {
        return false;
    }
    failed = fwrite(data, 1, length, file) != length;
    return finish_entry(crc32_update(0, data, length), length);
}

bool zip_writer::add_file(const char *name, FILE *source) {
    if (source == nullptr) {
        return false;
    }
    bool started = begin_entry(name);
    uint32_t crc = 0;
    uint32_t size = 0;
    uint8_t chunk[copy_chunk];
    size_t read;
    while (started && !failed && (read = fread(chunk, 1, sizeof(chunk), source)) > 0) {
        failed = fwrite(chunk, 1, read, file) != read;
        crc = crc32_update(crc, chunk, uint32_t(read));
        size += uint32_t(read);
    }
    fclose(source);
    return started && finish_entry(crc, size);
}

bool zip_writer::close() {
    if (file == nullptr) {
        return false;
    }
    uint32_t directory_offset = uint32_t(ftell(file));
    for (int index = 0; index < entry_count && !failed; index++) {
        const entry &current = entry_list[index];
        uint8_t header[46] = {};
        uint16_t name_length = uint16_t(strlen(current.name));
        put32(header, central_signature);
        put16(header + 4, version);
        put16(header + 6, version);
        put16(header + 8, utf8_names);
        put16(header + 12, dos_time);
        put16(header + 14, dos_date);
        put32(header + 16, current.crc);
        put32(header + 20, current.size);
        put32(header + 24, current.size);
        put16(header + 28, name_length);
        put32(header + 42, current.offset);
        failed = fwrite(header, 1, sizeof(header), file) != sizeof(header) || fwrite(current.name, 1, name_length, file) != name_length;
    }
    uint32_t directory_size = uint32_t(ftell(file)) - directory_offset;
    uint8_t end[22] = {};
    put32(end, end_signature);
    put16(end + 8, uint16_t(entry_count));
    put16(end + 10, uint16_t(entry_count));
    put32(end + 12, directory_size);
    put32(end + 16, directory_offset);
    failed = failed || fwrite(end, 1, sizeof(end), file) != sizeof(end);
    failed = fclose(file) != 0 || failed;
    file = nullptr;
    return !failed;
}

}
