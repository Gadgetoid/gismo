#pragma once

#include <stdint.h>

namespace gismo {

bool write_bmp565(const wchar_t *path, const uint16_t *pixels, int width, int height);
bool read_bmp565(const wchar_t *path, uint16_t *pixels, int width, int height);
void shrink565(const uint16_t *source, int width, int height, uint16_t *target, int factor);

}
