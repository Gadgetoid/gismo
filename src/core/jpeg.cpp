#include "jpeg.h"

#include <math.h>
#include <string.h>

namespace gismo {

namespace {

constexpr int dct_shift = 13;
constexpr int row_shift = 10;

const uint8_t zigzag[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

const uint8_t luminance_quantisation[64] = {
    16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55,
    14, 13, 16, 24, 40, 57, 69, 56, 14, 17, 22, 29, 51, 87, 80, 62,
    18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92,
    49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99,
};

const uint8_t chrominance_quantisation[64] = {
    17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99,
    24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
};

const uint8_t dc_luminance_counts[16] = {0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
const uint8_t dc_chrominance_counts[16] = {0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
const uint8_t dc_values[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

const uint8_t ac_luminance_counts[16] = {0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d};
const uint8_t ac_luminance_values[162] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
    0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
    0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
    0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
    0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
    0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
    0xf9, 0xfa,
};

const uint8_t ac_chrominance_counts[16] = {0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77};
const uint8_t ac_chrominance_values[162] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
    0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
    0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
    0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
    0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
    0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
    0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
    0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
    0xf9, 0xfa,
};

struct huffman_code {
    uint16_t code;
    uint8_t length;
};

struct huffman_table {
    huffman_code codes[256];
};

void build_huffman(const uint8_t *counts, const uint8_t *values, huffman_table &table) {
    memset(&table, 0, sizeof(table));
    int code = 0;
    int index = 0;
    for (int length = 1; length <= 16; length++) {
        for (int count = 0; count < counts[length - 1]; count++) {
            table.codes[values[index]].code = uint16_t(code);
            table.codes[values[index]].length = uint8_t(length);
            code++;
            index++;
        }
        code <<= 1;
    }
}

struct writer {
    uint8_t *output;
    int capacity;
    int position;
    uint32_t bits;
    int bit_count;
    bool overflow;

    void byte(uint8_t value) {
        if (position < capacity) {
            output[position++] = value;
        } else {
            overflow = true;
        }
    }

    void word(uint16_t value) {
        byte(uint8_t(value >> 8));
        byte(uint8_t(value));
    }

    void bytes(const void *data, int length) {
        const uint8_t *source = static_cast<const uint8_t *>(data);
        for (int index = 0; index < length; index++) {
            byte(source[index]);
        }
    }

    void put_bits(uint32_t value, int length) {
        bits = (bits << length) | (value & ((1u << length) - 1));
        bit_count += length;
        while (bit_count >= 8) {
            uint8_t next = uint8_t(bits >> (bit_count - 8));
            byte(next);
            if (next == 0xFF) {
                byte(0);
            }
            bit_count -= 8;
        }
    }

    void flush_bits() {
        if (bit_count > 0) {
            put_bits(0x7F, 8 - bit_count);
        }
        bits = 0;
        bit_count = 0;
    }
};

int dct_table[8][8];
bool dct_ready;

void prepare_dct() {
    if (dct_ready) {
        return;
    }
    for (int frequency = 0; frequency < 8; frequency++) {
        double scale = frequency == 0 ? sqrt(0.5) : 1.0;
        for (int position = 0; position < 8; position++) {
            double value = scale / 2.0 * cos((2 * position + 1) * frequency * 3.14159265358979323846 / 16.0);
            dct_table[frequency][position] = int(floor(value * (1 << dct_shift) + 0.5));
        }
    }
    dct_ready = true;
}

void forward_dct(const int *block, int *coefficients) {
    int rows[8][8];
    for (int y = 0; y < 8; y++) {
        for (int u = 0; u < 8; u++) {
            int sum = 0;
            for (int x = 0; x < 8; x++) {
                sum += block[y * 8 + x] * dct_table[u][x];
            }
            rows[y][u] = (sum + (1 << (row_shift - 1))) >> row_shift;
        }
    }
    constexpr int column_shift = dct_shift * 2 - row_shift;
    for (int v = 0; v < 8; v++) {
        for (int u = 0; u < 8; u++) {
            int sum = 0;
            for (int y = 0; y < 8; y++) {
                sum += rows[y][u] * dct_table[v][y];
            }
            coefficients[v * 8 + u] = (sum + (1 << (column_shift - 1))) >> column_shift;
        }
    }
}

int magnitude_bits(int value) {
    if (value < 0) {
        value = -value;
    }
    int bits = 0;
    while (value) {
        bits++;
        value >>= 1;
    }
    return bits;
}

void encode_block(writer &out, const int *block, const uint16_t *quantisation, int &previous_dc, const huffman_table &dc, const huffman_table &ac) {
    int coefficients[64];
    forward_dct(block, coefficients);
    int quantised[64];
    for (int index = 0; index < 64; index++) {
        int position = zigzag[index];
        int value = coefficients[position];
        int divisor = quantisation[index];
        quantised[index] = value >= 0 ? (value + divisor / 2) / divisor : -((-value + divisor / 2) / divisor);
    }

    int difference = quantised[0] - previous_dc;
    previous_dc = quantised[0];
    int size = magnitude_bits(difference);
    out.put_bits(dc.codes[size].code, dc.codes[size].length);
    if (size) {
        out.put_bits(difference < 0 ? difference - 1 : difference, size);
    }

    int zeros = 0;
    for (int index = 1; index < 64; index++) {
        int value = quantised[index];
        if (value == 0) {
            zeros++;
            continue;
        }
        while (zeros >= 16) {
            out.put_bits(ac.codes[0xF0].code, ac.codes[0xF0].length);
            zeros -= 16;
        }
        size = magnitude_bits(value);
        int symbol = (zeros << 4) | size;
        out.put_bits(ac.codes[symbol].code, ac.codes[symbol].length);
        out.put_bits(value < 0 ? value - 1 : value, size);
        zeros = 0;
    }
    if (zeros) {
        out.put_bits(ac.codes[0].code, ac.codes[0].length);
    }
}

void scale_quantisation(const uint8_t *base, int quality, uint16_t *scaled) {
    quality = quality < 1 ? 1 : quality > 100 ? 100 : quality;
    int factor = quality < 50 ? 5000 / quality : 200 - quality * 2;
    for (int index = 0; index < 64; index++) {
        int value = (base[zigzag[index]] * factor + 50) / 100;
        scaled[index] = uint16_t(value < 1 ? 1 : value > 255 ? 255 : value);
    }
}

void write_huffman_table(writer &out, int class_and_id, const uint8_t *counts, const uint8_t *values, int value_count) {
    out.byte(uint8_t(class_and_id));
    out.bytes(counts, 16);
    out.bytes(values, value_count);
}

struct tiff_builder {
    uint8_t data[512];
    int length;

    void u16(int offset, uint16_t value) {
        data[offset] = uint8_t(value);
        data[offset + 1] = uint8_t(value >> 8);
    }

    void u32(int offset, uint32_t value) {
        u16(offset, uint16_t(value));
        u16(offset + 2, uint16_t(value >> 16));
    }

    int reserve(int size) {
        int start = length;
        length += (size + 1) & ~1;
        return start;
    }

    void entry(int ifd, int index, uint16_t tag, uint16_t type, uint32_t count, uint32_t value) {
        int offset = ifd + 2 + index * 12;
        u16(offset, tag);
        u16(offset + 2, type);
        u32(offset + 4, count);
        u32(offset + 8, value);
    }

    uint32_t ascii(const char *text) {
        int size = int(strlen(text)) + 1;
        int start = reserve(size);
        memcpy(data + start, text, size);
        return uint32_t(start);
    }

    uint32_t rationals(const uint32_t *values, int count) {
        int start = reserve(count * 8);
        for (int index = 0; index < count; index++) {
            u32(start + index * 8, values[index * 2]);
            u32(start + index * 8 + 4, values[index * 2 + 1]);
        }
        return uint32_t(start);
    }
};

constexpr uint16_t type_byte = 1;
constexpr uint16_t type_ascii = 2;
constexpr uint16_t type_short = 3;
constexpr uint16_t type_long = 4;
constexpr uint16_t type_rational = 5;

void degrees_to_rationals(double value, uint32_t *rationals) {
    value = fabs(value);
    uint32_t degrees = uint32_t(value);
    double minutes_value = (value - degrees) * 60.0;
    uint32_t minutes = uint32_t(minutes_value);
    uint32_t seconds = uint32_t(floor((minutes_value - minutes) * 60.0 * 10000.0 + 0.5));
    rationals[0] = degrees;
    rationals[1] = 1;
    rationals[2] = minutes;
    rationals[3] = 1;
    rationals[4] = seconds;
    rationals[5] = 10000;
}

int append_number(char *text, int position, int value, int digits, char separator) {
    for (int digit = digits - 1; digit >= 0; digit--) {
        text[position + digit] = char('0' + value % 10);
        value /= 10;
    }
    position += digits;
    if (separator) {
        text[position++] = separator;
    }
    text[position] = 0;
    return position;
}

void write_exif(writer &out, const photo_tag &tag) {
    static tiff_builder tiff;
    memset(&tiff, 0, sizeof(tiff));
    tiff.data[0] = 'I';
    tiff.data[1] = 'I';
    tiff.u16(2, 42);
    tiff.u32(4, 8);

    int ifd0_count = tag.has_time ? 4 : 3;
    if (tag.has_position) {
        ifd0_count++;
    }
    int ifd0 = 8;
    tiff.length = ifd0 + 2 + ifd0_count * 12 + 4;
    tiff.u16(ifd0, uint16_t(ifd0_count));

    int gps_count = 0;
    int gps = 0;
    if (tag.has_position) {
        gps_count = tag.has_time ? 9 : 7;
        gps = tiff.reserve(2 + gps_count * 12 + 4);
        tiff.u16(gps, uint16_t(gps_count));
    }

    int index = 0;
    tiff.entry(ifd0, index++, 0x010F, type_ascii, 6, tiff.ascii("GISmo"));
    tiff.entry(ifd0, index++, 0x0110, type_ascii, 16, tiff.ascii("Trimble Juno SB"));
    tiff.entry(ifd0, index++, 0x0112, type_short, 1, 1);
    if (tag.has_time) {
        const utc_time &time = tag.time;
        char stamp[24];
        int written = append_number(stamp, 0, time.year, 4, ':');
        written = append_number(stamp, written, time.month, 2, ':');
        written = append_number(stamp, written, time.day, 2, ' ');
        written = append_number(stamp, written, time.hour, 2, ':');
        written = append_number(stamp, written, time.minute, 2, ':');
        append_number(stamp, written, time.second, 2, 0);
        tiff.entry(ifd0, index++, 0x0132, type_ascii, 20, tiff.ascii(stamp));
    }
    if (tag.has_position) {
        tiff.entry(ifd0, index++, 0x8825, type_long, 1, uint32_t(gps));

        int gps_index = 0;
        tiff.entry(gps, gps_index++, 0x0000, type_byte, 4, 0x00000302);
        tiff.entry(gps, gps_index++, 0x0001, type_ascii, 2, tag.latitude >= 0 ? 'N' : 'S');
        uint32_t latitude[6];
        degrees_to_rationals(tag.latitude, latitude);
        tiff.entry(gps, gps_index++, 0x0002, type_rational, 3, tiff.rationals(latitude, 3));
        tiff.entry(gps, gps_index++, 0x0003, type_ascii, 2, tag.longitude >= 0 ? 'E' : 'W');
        uint32_t longitude[6];
        degrees_to_rationals(tag.longitude, longitude);
        tiff.entry(gps, gps_index++, 0x0004, type_rational, 3, tiff.rationals(longitude, 3));
        tiff.entry(gps, gps_index++, 0x0005, type_byte, 1, tag.altitude < 0 ? 1 : 0);
        uint32_t altitude[2] = {uint32_t(floor(fabs(tag.altitude) * 10 + 0.5)), 10};
        tiff.entry(gps, gps_index++, 0x0006, type_rational, 1, tiff.rationals(altitude, 1));
        if (tag.has_time) {
            const utc_time &time = tag.time;
            uint32_t clock[6] = {uint32_t(time.hour), 1, uint32_t(time.minute), 1, uint32_t(time.second * 1000 + time.millisecond), 1000};
            tiff.entry(gps, gps_index++, 0x0007, type_rational, 3, tiff.rationals(clock, 3));
            char date[12];
            int written = append_number(date, 0, time.year, 4, ':');
            written = append_number(date, written, time.month, 2, ':');
            append_number(date, written, time.day, 2, 0);
            tiff.entry(gps, gps_index++, 0x001D, type_ascii, 11, tiff.ascii(date));
        }
    }

    out.word(0xFFE1);
    out.word(uint16_t(2 + 6 + tiff.length));
    out.bytes("Exif\0\0", 6);
    out.bytes(tiff.data, tiff.length);
}

uint8_t expand5(int value) {
    return uint8_t((value << 3) | (value >> 2));
}

uint8_t expand6(int value) {
    return uint8_t((value << 2) | (value >> 4));
}

}

int encode_jpeg(const uint16_t *pixels, int width, int height, int quality, const photo_tag *tag, uint8_t *output, int capacity) {
    prepare_dct();
    static huffman_table dc_luminance;
    static huffman_table dc_chrominance;
    static huffman_table ac_luminance;
    static huffman_table ac_chrominance;
    build_huffman(dc_luminance_counts, dc_values, dc_luminance);
    build_huffman(dc_chrominance_counts, dc_values, dc_chrominance);
    build_huffman(ac_luminance_counts, ac_luminance_values, ac_luminance);
    build_huffman(ac_chrominance_counts, ac_chrominance_values, ac_chrominance);

    uint16_t luminance_table[64];
    uint16_t chrominance_table[64];
    scale_quantisation(luminance_quantisation, quality, luminance_table);
    scale_quantisation(chrominance_quantisation, quality, chrominance_table);

    writer out = {output, capacity, 0, 0, 0, false};
    out.word(0xFFD8);
    if (tag) {
        write_exif(out, *tag);
    }

    out.word(0xFFDB);
    out.word(2 + 65 * 2);
    out.byte(0);
    for (int index = 0; index < 64; index++) {
        out.byte(uint8_t(luminance_table[index]));
    }
    out.byte(1);
    for (int index = 0; index < 64; index++) {
        out.byte(uint8_t(chrominance_table[index]));
    }

    out.word(0xFFC0);
    out.word(17);
    out.byte(8);
    out.word(uint16_t(height));
    out.word(uint16_t(width));
    out.byte(3);
    const uint8_t components[9] = {1, 0x22, 0, 2, 0x11, 1, 3, 0x11, 1};
    out.bytes(components, 9);

    out.word(0xFFC4);
    out.word(2 + (1 + 16 + 12) * 2 + (1 + 16 + 162) * 2);
    write_huffman_table(out, 0x00, dc_luminance_counts, dc_values, 12);
    write_huffman_table(out, 0x10, ac_luminance_counts, ac_luminance_values, 162);
    write_huffman_table(out, 0x01, dc_chrominance_counts, dc_values, 12);
    write_huffman_table(out, 0x11, ac_chrominance_counts, ac_chrominance_values, 162);

    out.word(0xFFDA);
    out.word(12);
    out.byte(3);
    const uint8_t scan[6] = {1, 0x00, 2, 0x11, 3, 0x11};
    out.bytes(scan, 6);
    out.byte(0);
    out.byte(63);
    out.byte(0);

    int previous[3] = {0, 0, 0};
    int luma[4][64];
    int blue[64];
    int red[64];
    for (int top = 0; top < height; top += 16) {
        for (int left = 0; left < width; left += 16) {
            int blue_sum[64] = {};
            int red_sum[64] = {};
            for (int y = 0; y < 16; y++) {
                int source_y = top + y < height ? top + y : height - 1;
                for (int x = 0; x < 16; x++) {
                    int source_x = left + x < width ? left + x : width - 1;
                    uint16_t pixel = pixels[source_y * width + source_x];
                    int r = expand5(pixel >> 11);
                    int g = expand6((pixel >> 5) & 0x3F);
                    int b = expand5(pixel & 0x1F);
                    int luminance = (19595 * r + 38470 * g + 7471 * b + 32768) >> 16;
                    int chroma_blue = (-11059 * r - 21709 * g + 32768 * b + 32768) >> 16;
                    int chroma_red = (32768 * r - 27439 * g - 5329 * b + 32768) >> 16;
                    luma[(y / 8) * 2 + x / 8][(y % 8) * 8 + x % 8] = luminance - 128;
                    blue_sum[(y / 2) * 8 + x / 2] += chroma_blue;
                    red_sum[(y / 2) * 8 + x / 2] += chroma_red;
                }
            }
            for (int index = 0; index < 64; index++) {
                blue[index] = (blue_sum[index] + (blue_sum[index] >= 0 ? 2 : -2)) / 4;
                red[index] = (red_sum[index] + (red_sum[index] >= 0 ? 2 : -2)) / 4;
            }
            for (int block = 0; block < 4; block++) {
                encode_block(out, luma[block], luminance_table, previous[0], dc_luminance, ac_luminance);
            }
            encode_block(out, blue, chrominance_table, previous[1], dc_chrominance, ac_chrominance);
            encode_block(out, red, chrominance_table, previous[2], dc_chrominance, ac_chrominance);
        }
    }
    out.flush_bits();
    out.word(0xFFD9);
    return out.overflow ? -1 : out.position;
}

}
