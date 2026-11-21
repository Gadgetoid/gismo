#include "inflate.h"

#include <string.h>

namespace gismo {

namespace {

constexpr int max_bits = 15;
constexpr int fast_bits = 9;
constexpr int max_literal_lengths = 288;
constexpr int max_distances = 32;

const uint16_t length_base[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t length_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t distance_base[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const uint8_t distance_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
const uint8_t code_length_order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

struct huffman {
    uint16_t fast[1 << fast_bits];
    uint16_t counts[max_bits + 1];
    uint16_t symbols[max_literal_lengths];
};

struct bit_reader {
    const uint8_t *input;
    int input_length;
    int position;
    uint32_t bits;
    int bit_count;
    bool overrun;

    void refill() {
        while (bit_count <= 24) {
            uint32_t byte = 0;
            if (position < input_length) {
                byte = input[position];
            } else if (position > input_length + 4) {
                overrun = true;
            }
            position++;
            bits |= byte << bit_count;
            bit_count += 8;
        }
    }

    uint32_t take(int count) {
        if (count == 0) {
            return 0;
        }
        refill();
        uint32_t value = bits & ((1u << count) - 1);
        bits >>= count;
        bit_count -= count;
        return value;
    }
};

uint32_t reverse_bits(uint32_t value, int count) {
    uint32_t result = 0;
    for (int index = 0; index < count; index++) {
        result = (result << 1) | (value & 1);
        value >>= 1;
    }
    return result;
}

bool build(huffman &table, const uint8_t *lengths, int count) {
    memset(table.counts, 0, sizeof(table.counts));
    for (int symbol = 0; symbol < count; symbol++) {
        table.counts[lengths[symbol]]++;
    }
    table.counts[0] = 0;

    int left = 1;
    for (int length = 1; length <= max_bits; length++) {
        left = (left << 1) - table.counts[length];
        if (left < 0) {
            return false;
        }
    }

    uint16_t offsets[max_bits + 2];
    offsets[1] = 0;
    for (int length = 1; length <= max_bits; length++) {
        offsets[length + 1] = offsets[length] + table.counts[length];
    }
    for (int symbol = 0; symbol < count; symbol++) {
        if (lengths[symbol]) {
            table.symbols[offsets[lengths[symbol]]++] = uint16_t(symbol);
        }
    }

    memset(table.fast, 0, sizeof(table.fast));
    int code = 0;
    int index = 0;
    for (int length = 1; length <= fast_bits; length++) {
        for (int remaining = table.counts[length]; remaining > 0; remaining--) {
            uint16_t symbol = table.symbols[index++];
            uint32_t reversed = reverse_bits(code, length);
            for (uint32_t fill = reversed; fill < (1u << fast_bits); fill += 1u << length) {
                table.fast[fill] = uint16_t(symbol << 4 | length);
            }
            code++;
        }
        code <<= 1;
    }
    return true;
}

int decode(bit_reader &reader, const huffman &table) {
    reader.refill();
    uint16_t entry = table.fast[reader.bits & ((1 << fast_bits) - 1)];
    if (entry) {
        int length = entry & 15;
        reader.bits >>= length;
        reader.bit_count -= length;
        return entry >> 4;
    }
    int code = 0;
    int first = 0;
    int index = 0;
    for (int length = 1; length <= max_bits; length++) {
        code |= reader.take(1);
        int count = table.counts[length];
        if (code - first < count) {
            return table.symbols[index + code - first];
        }
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return -1;
}

struct tables {
    huffman literal_lengths;
    huffman distances;
};

bool build_fixed(tables &result) {
    uint8_t lengths[max_literal_lengths + max_distances];
    int symbol = 0;
    for (; symbol < 144; symbol++) {
        lengths[symbol] = 8;
    }
    for (; symbol < 256; symbol++) {
        lengths[symbol] = 9;
    }
    for (; symbol < 280; symbol++) {
        lengths[symbol] = 7;
    }
    for (; symbol < 288; symbol++) {
        lengths[symbol] = 8;
    }
    for (int distance = 0; distance < 30; distance++) {
        lengths[max_literal_lengths + distance] = 5;
    }
    return build(result.literal_lengths, lengths, 288) && build(result.distances, lengths + max_literal_lengths, 30);
}

bool build_dynamic(bit_reader &reader, tables &result) {
    int literal_count = int(reader.take(5)) + 257;
    int distance_count = int(reader.take(5)) + 1;
    int code_length_count = int(reader.take(4)) + 4;
    if (literal_count > 286 || distance_count > 30) {
        return false;
    }

    uint8_t code_lengths[19] = {};
    for (int index = 0; index < code_length_count; index++) {
        code_lengths[code_length_order[index]] = uint8_t(reader.take(3));
    }
    huffman code_length_table;
    if (!build(code_length_table, code_lengths, 19)) {
        return false;
    }

    uint8_t lengths[max_literal_lengths + max_distances] = {};
    int total = literal_count + distance_count;
    int index = 0;
    while (index < total) {
        int symbol = decode(reader, code_length_table);
        if (symbol < 0) {
            return false;
        }
        if (symbol < 16) {
            lengths[index++] = uint8_t(symbol);
            continue;
        }
        int repeat;
        uint8_t value = 0;
        if (symbol == 16) {
            if (index == 0) {
                return false;
            }
            value = lengths[index - 1];
            repeat = 3 + int(reader.take(2));
        } else if (symbol == 17) {
            repeat = 3 + int(reader.take(3));
        } else {
            repeat = 11 + int(reader.take(7));
        }
        if (index + repeat > total) {
            return false;
        }
        while (repeat--) {
            lengths[index++] = value;
        }
    }
    if (lengths[256] == 0) {
        return false;
    }
    return build(result.literal_lengths, lengths, literal_count) && build(result.distances, lengths + literal_count, distance_count);
}

bool inflate_block(bit_reader &reader, const tables &codes, uint8_t *output, int output_length, int &written) {
    for (;;) {
        int symbol = decode(reader, codes.literal_lengths);
        if (symbol < 0 || reader.overrun) {
            return false;
        }
        if (symbol < 256) {
            if (written >= output_length) {
                return false;
            }
            output[written++] = uint8_t(symbol);
            continue;
        }
        if (symbol == 256) {
            return true;
        }
        symbol -= 257;
        if (symbol >= 29) {
            return false;
        }
        int length = length_base[symbol] + int(reader.take(length_extra[symbol]));
        int distance_symbol = decode(reader, codes.distances);
        if (distance_symbol < 0 || distance_symbol >= 30) {
            return false;
        }
        int distance = distance_base[distance_symbol] + int(reader.take(distance_extra[distance_symbol]));
        if (distance > written || written + length > output_length) {
            return false;
        }
        uint8_t *target = output + written;
        const uint8_t *source = target - distance;
        for (int index = 0; index < length; index++) {
            target[index] = source[index];
        }
        written += length;
    }
}

}

bool zlib_inflate(const uint8_t *input, int input_length, uint8_t *output, int output_length) {
    if (input_length < 6) {
        return false;
    }
    int method = input[0] & 15;
    if (method != 8 || ((input[0] << 8) | input[1]) % 31 != 0 || (input[1] & 0x20)) {
        return false;
    }

    bit_reader reader = {input + 2, input_length - 6, 0, 0, 0, false};
    static tables fixed;
    static bool fixed_built = build_fixed(fixed);
    tables dynamic;
    int written = 0;
    bool last = false;
    while (!last) {
        last = reader.take(1);
        int type = int(reader.take(2));
        if (type == 0) {
            reader.bits >>= reader.bit_count & 7;
            reader.bit_count -= reader.bit_count & 7;
            int stored_position = reader.position - reader.bit_count / 8;
            reader.bits = 0;
            reader.bit_count = 0;
            if (stored_position + 4 > reader.input_length) {
                return false;
            }
            const uint8_t *header = reader.input + stored_position;
            int length = header[0] | header[1] << 8;
            int complement = header[2] | header[3] << 8;
            if ((length ^ 0xFFFF) != complement || stored_position + 4 + length > reader.input_length || written + length > output_length) {
                return false;
            }
            memcpy(output + written, header + 4, length);
            written += length;
            reader.position = stored_position + 4 + length;
        } else if (type == 1) {
            if (!fixed_built || !inflate_block(reader, fixed, output, output_length, written)) {
                return false;
            }
        } else if (type == 2) {
            if (!build_dynamic(reader, dynamic) || !inflate_block(reader, dynamic, output, output_length, written)) {
                return false;
            }
        } else {
            return false;
        }
    }
    if (written != output_length) {
        return false;
    }

    uint32_t a = 1;
    uint32_t b = 0;
    const uint8_t *next = output;
    int remaining = written;
    while (remaining > 0) {
        int chunk = remaining < 5552 ? remaining : 5552;
        remaining -= chunk;
        while (chunk--) {
            a += *next++;
            b += a;
        }
        a %= 65521;
        b %= 65521;
    }
    const uint8_t *trailer = input + input_length - 4;
    uint32_t expected = uint32_t(trailer[0]) << 24 | uint32_t(trailer[1]) << 16 | uint32_t(trailer[2]) << 8 | trailer[3];
    return ((b << 16) | a) == expected;
}

}
