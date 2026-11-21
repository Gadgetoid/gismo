#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "map_pack.h"

using namespace gismo;

static uint32_t fnv1a(uint32_t hash, const void *data, size_t length) {
    const uint8_t *bytes = static_cast<const uint8_t *>(data);
    for (size_t index = 0; index < length; index++) {
        hash = (hash ^ bytes[index]) * 16777619u;
    }
    return hash;
}

static int decode_all(map_pack &pack) {
    static map_tile tile;
    int failures = 0;
    clock_t started = clock();
    int decoded = 0;
    for (int number = 0; number < pack.level_count(); number++) {
        const map_level &level = pack.level(number);
        int span = level.span();
        uint32_t hash = 2166136261u;
        int tiles = 0;
        for (int index_row = 0; index_row < int(level.rows); index_row++) {
            for (int index_column = 0; index_column < int(level.columns); index_column++) {
                int column = level.west / span + index_column;
                int row = level.north / span - 1 - index_row;
                if (!pack.has_tile(number, column, row)) {
                    continue;
                }
                if (!pack.read_tile(number, column, row, tile)) {
                    printf("level %d tile %d,%d failed\n", number, column, row);
                    failures++;
                    continue;
                }
                hash = fnv1a(hash, tile.palette, tile.colour_count * 2);
                hash = fnv1a(hash, tile.pixels, sizeof(tile.pixels));
                tiles++;
            }
        }
        decoded += tiles;
        printf("level %d %d %08x\n", number, tiles, hash);
    }
    double seconds = double(clock() - started) / CLOCKS_PER_SEC;
    fprintf(stderr, "%d tiles in %.2f s, %.2f ms per tile\n", decoded, seconds, decoded ? seconds * 1000 / decoded : 0);
    return failures ? 1 : 0;
}

static int render(map_pack &pack, char **arguments) {
    map_view view = {atof(arguments[0]), atof(arguments[1]), atof(arguments[4])};
    int width = atoi(arguments[2]);
    int height = atoi(arguments[3]);
    framebuffer target = {static_cast<uint16_t *>(malloc(width * height * 2)), width, height, width};
    map_tile_cache cache(24);
    draw_map(pack, cache, view, target, map_fill::background, 0x0000);

    FILE *output = fopen(arguments[5], "wb");
    fwrite(target.pixels, 2, width * height, output);
    fclose(output);
    free(target.pixels);
    printf("%d\n", best_level(pack, view.metres_per_pixel));
    return 0;
}

int main(int count, char **arguments) {
    if (count < 3) {
        fprintf(stderr, "usage: map_tool PACK decode | render EAST NORTH WIDTH HEIGHT METRES_PER_PIXEL OUTPUT\n");
        return 2;
    }
    map_pack pack;
    if (!pack.open(fopen(arguments[1], "rb"))) {
        fprintf(stderr, "can't open %s\n", arguments[1]);
        return 1;
    }
    if (strcmp(arguments[2], "decode") == 0) {
        return decode_all(pack);
    }
    if (strcmp(arguments[2], "render") == 0 && count == 9) {
        return render(pack, arguments + 3);
    }
    return 2;
}
