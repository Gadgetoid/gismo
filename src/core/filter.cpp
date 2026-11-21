#include "filter.h"

namespace gismo {

namespace {

constexpr int matrix_one = 1024;

struct number_reader {
    const char *text;
    int length;
    int position;

    bool next_line() {
        while (position < length && text[position] != '\n') {
            position++;
        }
        if (position >= length) {
            return false;
        }
        position++;
        return true;
    }

    bool number(int &value) {
        while (position < length && (text[position] == ' ' || text[position] == '\t' || text[position] == ',')) {
            position++;
        }
        if (position >= length || text[position] == '\n' || text[position] == '\r') {
            return false;
        }
        bool negative = text[position] == '-';
        if (negative || text[position] == '+') {
            position++;
        }
        if (position >= length || text[position] < '0' || text[position] > '9') {
            return false;
        }
        value = 0;
        while (position < length && text[position] >= '0' && text[position] <= '9') {
            value = value * 10 + (text[position] - '0');
            position++;
        }
        if (negative) {
            value = -value;
        }
        return true;
    }
};

int clamp_byte(int value) {
    return value < 0 ? 0 : value > 255 ? 255 : value;
}

}

bool parse_filter(const char *text, int length, colour_filter &result) {
    number_reader reader = {text, length, 0};
    if (!reader.next_line()) {
        return false;
    }
    for (int row = 0; row < 3; row++) {
        for (int column = 0; column < 3; column++) {
            if (!reader.number(result.matrix[row][column])) {
                return false;
            }
        }
        if (!reader.next_line()) {
            return false;
        }
    }
    for (int channel = 0; channel < 3; channel++) {
        for (int index = 0; index < 256; index++) {
            int value;
            if (!reader.number(value)) {
                return false;
            }
            result.curves[channel][index] = uint8_t(clamp_byte(value));
        }
        if (channel < 2 && !reader.next_line()) {
            return false;
        }
    }
    return true;
}

void build_filter_table(const colour_filter &filter, uint16_t *table) {
    for (int pixel = 0; pixel < 65536; pixel++) {
        int red5 = pixel >> 11;
        int green6 = (pixel >> 5) & 0x3F;
        int blue5 = pixel & 0x1F;
        int input[3] = {(red5 << 3) | (red5 >> 2), (green6 << 2) | (green6 >> 4), (blue5 << 3) | (blue5 >> 2)};
        int output[3];
        for (int channel = 0; channel < 3; channel++) {
            const int *weights = filter.matrix[channel];
            int mixed = (weights[0] * input[0] + weights[1] * input[1] + weights[2] * input[2] + matrix_one / 2) / matrix_one;
            output[channel] = filter.curves[channel][clamp_byte(mixed)];
        }
        table[pixel] = uint16_t((output[0] >> 3) << 11 | (output[1] >> 2) << 5 | output[2] >> 3);
    }
}

}
