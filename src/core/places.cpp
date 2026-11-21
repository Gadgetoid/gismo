#include "places.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace gismo {

namespace {

constexpr int header_size = 56;

uint32_t read_u32(const uint8_t *bytes) {
    return uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8 | uint32_t(bytes[2]) << 16 | uint32_t(bytes[3]) << 24;
}

uint16_t read_u16(const uint8_t *bytes) {
    return uint16_t(bytes[0] | bytes[1] << 8);
}

int insert_nearby(nearby_place *results, int count, int capacity, const nearby_place &candidate) {
    if (count == capacity && candidate.distance >= results[count - 1].distance) {
        return count;
    }
    int position = count < capacity ? count : capacity - 1;
    while (position > 0 && results[position - 1].distance > candidate.distance) {
        results[position] = results[position - 1];
        position--;
    }
    results[position] = candidate;
    return count < capacity ? count + 1 : count;
}

}

place_index::place_index() : data(nullptr), place_count(0), west(0), south(0), cell_size(0), columns(0), rows(0), cells(nullptr), records(nullptr), names(nullptr), names_length(0), kind_count(0) {
}

place_index::~place_index() {
    release();
}

void place_index::release() {
    free(data);
    data = nullptr;
    place_count = 0;
    kind_count = 0;
}

bool place_index::load(FILE *file) {
    release();
    if (file == nullptr) {
        return false;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size > header_size) {
        data = static_cast<uint8_t *>(malloc(size));
    }
    bool ok = data && fread(data, 1, size, file) == size_t(size);
    fclose(file);
    ok = ok && memcmp(data, "GISMOPLC", 8) == 0 && read_u16(data + 8) == 1;
    if (!ok) {
        release();
        return false;
    }
    int kinds = read_u16(data + 10);
    uint32_t total = read_u32(data + 12);
    west = int32_t(read_u32(data + 16));
    south = int32_t(read_u32(data + 20));
    cell_size = int32_t(read_u32(data + 24));
    columns = int32_t(read_u32(data + 28));
    rows = int32_t(read_u32(data + 32));
    uint32_t kinds_offset = read_u32(data + 36);
    uint32_t cells_offset = read_u32(data + 40);
    uint32_t records_offset = read_u32(data + 44);
    uint32_t names_offset = read_u32(data + 48);
    names_length = read_u32(data + 52);
    uint64_t cell_count = uint64_t(columns) * uint64_t(rows) + 1;
    ok = kinds <= max_place_kinds && cell_size > 0 && columns > 0 && rows > 0 && cells_offset % 4 == 0 && records_offset % 4 == 0;
    ok = ok && cells_offset + cell_count * 4 <= uint64_t(size) && records_offset + uint64_t(total) * sizeof(record) <= uint64_t(size);
    ok = ok && uint64_t(names_offset) + names_length <= uint64_t(size) && names_length > 0;
    uint32_t position = kinds_offset;
    for (int kind = 0; ok && kind < kinds; kind++) {
        ok = position + 2 <= uint32_t(size) && data[position] < place_category_count;
        int length = ok ? data[position + 1] : 0;
        ok = ok && length < int(sizeof(kind_labels[0])) && position + 2 + length <= uint32_t(size);
        if (ok) {
            kind_categories[kind] = data[position];
            memcpy(kind_labels[kind], data + position + 2, length);
            kind_labels[kind][length] = 0;
            position += 2 + length;
        }
    }
    if (ok) {
        cells = reinterpret_cast<const uint32_t *>(data + cells_offset);
        records = reinterpret_cast<const record *>(data + records_offset);
        names = reinterpret_cast<const char *>(data + names_offset);
        ok = cells[cell_count - 1] == total && names[names_length - 1] == 0;
        for (uint32_t index = 0; ok && index < total; index++) {
            ok = records[index].kind < kinds && records[index].name < names_length;
        }
    }
    if (!ok) {
        release();
        return false;
    }
    kind_count = kinds;
    place_count = total;
    return true;
}

double place_index::east(uint32_t index) const {
    return records[index].east;
}

double place_index::north(uint32_t index) const {
    return records[index].north;
}

const char *place_index::name(uint32_t index) const {
    const char *value = names + records[index].name;
    return value[0] ? value : kind_labels[records[index].kind];
}

const char *place_index::kind_label(uint32_t index) const {
    return kind_labels[records[index].kind];
}

int place_index::category(uint32_t index) const {
    return kind_categories[records[index].kind];
}

int place_index::nearest(double target_east, double target_north, int wanted_category, nearby_place *results, int count, int capacity) const {
    if (!loaded() || capacity <= 0) {
        return count;
    }
    int centre_column = int(floor((target_east - west) / cell_size));
    int centre_row = int(floor((target_north - south) / cell_size));
    int last_ring = 0;
    int reaches[4] = {centre_column, columns - 1 - centre_column, centre_row, rows - 1 - centre_row};
    for (int reach : reaches) {
        int distance = reach < 0 ? -reach : reach;
        last_ring = distance > last_ring ? distance : last_ring;
    }
    for (int ring = 0; ring <= last_ring; ring++) {
        if (count == capacity && results[count - 1].distance <= double(ring - 1) * cell_size) {
            break;
        }
        for (int row = centre_row - ring; row <= centre_row + ring; row++) {
            if (row < 0 || row >= rows) {
                continue;
            }
            bool edge_row = row == centre_row - ring || row == centre_row + ring;
            int step = edge_row || ring == 0 ? 1 : 2 * ring;
            for (int column = centre_column - ring; column <= centre_column + ring; column += step) {
                if (column < 0 || column >= columns) {
                    continue;
                }
                uint32_t cell = uint32_t(row) * uint32_t(columns) + uint32_t(column);
                for (uint32_t index = cells[cell]; index < cells[cell + 1]; index++) {
                    if (wanted_category >= 0 && category(index) != wanted_category) {
                        continue;
                    }
                    nearby_place candidate = {this, index, hypot(records[index].east - target_east, records[index].north - target_north)};
                    count = insert_nearby(results, count, capacity, candidate);
                }
            }
        }
    }
    return count;
}

}
