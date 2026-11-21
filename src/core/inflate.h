#pragma once

#include <stdint.h>

namespace gismo {

bool zlib_inflate(const uint8_t *input, int input_length, uint8_t *output, int output_length);

}
