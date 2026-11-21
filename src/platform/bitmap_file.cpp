#include "bitmap_file.h"

#include <windows.h>

#include <stdlib.h>
#include <string.h>

namespace gismo {

namespace {

struct bmp_header {
    uint8_t magic[2];
    uint32_t file_size;
    uint32_t reserved;
    uint32_t pixel_offset;
    uint32_t header_size;
    int32_t width;
    int32_t height;
    uint16_t planes;
    uint16_t bit_count;
    uint32_t compression;
    uint32_t image_size;
    int32_t x_pixels_per_metre;
    int32_t y_pixels_per_metre;
    uint32_t colours_used;
    uint32_t colours_important;
    uint32_t masks[3];
} __attribute__((packed));

}

bool write_bmp565(const wchar_t *path, const uint16_t *pixels, int width, int height) {
    int row_bytes = (width * 2 + 3) & ~3;
    bmp_header header = {};
    header.magic[0] = 'B';
    header.magic[1] = 'M';
    header.pixel_offset = sizeof(header);
    header.file_size = sizeof(header) + row_bytes * height;
    header.header_size = 40;
    header.width = width;
    header.height = height;
    header.planes = 1;
    header.bit_count = 16;
    header.compression = BI_BITFIELDS;
    header.image_size = row_bytes * height;
    header.x_pixels_per_metre = 2835;
    header.y_pixels_per_metre = 2835;
    header.masks[0] = 0xF800;
    header.masks[1] = 0x07E0;
    header.masks[2] = 0x001F;

    uint8_t *data = static_cast<uint8_t *>(malloc(header.file_size));
    if (data == nullptr) {
        return false;
    }
    memcpy(data, &header, sizeof(header));
    for (int y = 0; y < height; y++) {
        uint8_t *row = data + sizeof(header) + (height - 1 - y) * row_bytes;
        memset(row, 0, row_bytes);
        memcpy(row, pixels + y * width, width * 2);
    }
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD written = 0;
    bool ok = file != INVALID_HANDLE_VALUE && WriteFile(file, data, header.file_size, &written, nullptr) && written == header.file_size;
    if (file != INVALID_HANDLE_VALUE) {
        CloseHandle(file);
    }
    free(data);
    return ok;
}

bool read_bmp565(const wchar_t *path, uint16_t *pixels, int width, int height) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    bmp_header header;
    DWORD read = 0;
    bool ok = ReadFile(file, &header, sizeof(header), &read, nullptr) && read == sizeof(header) && header.magic[0] == 'B' && header.magic[1] == 'M' && header.bit_count == 16 && header.width == width && (header.height == height || header.height == -height);
    int row_bytes = (width * 2 + 3) & ~3;
    if (ok) {
        SetFilePointer(file, header.pixel_offset, nullptr, FILE_BEGIN);
        uint8_t *data = static_cast<uint8_t *>(malloc(row_bytes * height));
        ok = data && ReadFile(file, data, row_bytes * height, &read, nullptr) && read == DWORD(row_bytes * height);
        if (ok) {
            for (int y = 0; y < height; y++) {
                int source_row = header.height > 0 ? height - 1 - y : y;
                memcpy(pixels + y * width, data + source_row * row_bytes, width * 2);
            }
        }
        free(data);
    }
    CloseHandle(file);
    return ok;
}

void shrink565(const uint16_t *source, int width, int height, uint16_t *target, int factor) {
    int target_width = width / factor;
    int target_height = height / factor;
    int samples = factor * factor;
    for (int y = 0; y < target_height; y++) {
        for (int x = 0; x < target_width; x++) {
            int red = 0;
            int green = 0;
            int blue = 0;
            for (int dy = 0; dy < factor; dy++) {
                const uint16_t *row = source + (y * factor + dy) * width + x * factor;
                for (int dx = 0; dx < factor; dx++) {
                    uint16_t pixel = row[dx];
                    red += pixel >> 11;
                    green += (pixel >> 5) & 0x3F;
                    blue += pixel & 0x1F;
                }
            }
            target[y * target_width + x] = uint16_t((red / samples) << 11 | (green / samples) << 5 | (blue / samples));
        }
    }
}

}
