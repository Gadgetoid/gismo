#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "jpeg.h"

using namespace gismo;

int main(int count, char **arguments) {
    if (count != 6) {
        fprintf(stderr, "usage: jpeg_tool INPUT.raw WIDTH HEIGHT QUALITY OUTPUT.jpg\n");
        return 2;
    }
    int width = atoi(arguments[2]);
    int height = atoi(arguments[3]);
    uint16_t *pixels = static_cast<uint16_t *>(malloc(width * height * 2));
    FILE *input = fopen(arguments[1], "rb");
    if (input == nullptr || fread(pixels, 2, width * height, input) != size_t(width * height)) {
        fprintf(stderr, "can't read %s\n", arguments[1]);
        return 1;
    }
    fclose(input);

    photo_tag tag = {};
    tag.has_position = true;
    tag.latitude = 52.2525051;
    tag.longitude = 0.0184061;
    tag.altitude = 21.4;
    tag.has_time = true;
    tag.time = {2026, 11, 14, 12, 42, 15, 500};

    static uint8_t output[512 * 1024];
    clock_t started = clock();
    int length = 0;
    for (int run = 0; run < 20; run++) {
        length = encode_jpeg(pixels, width, height, atoi(arguments[4]), &tag, output, sizeof(output));
    }
    double milliseconds = double(clock() - started) * 1000 / CLOCKS_PER_SEC / 20;
    if (length < 0) {
        fprintf(stderr, "output buffer too small\n");
        return 1;
    }
    FILE *file = fopen(arguments[5], "wb");
    fwrite(output, 1, length, file);
    fclose(file);
    printf("%d bytes in %.2f ms\n", length, milliseconds);
    free(pixels);
    return 0;
}
