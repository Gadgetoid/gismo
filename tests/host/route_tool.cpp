#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "route.h"

using namespace gismo;

int main(int count, char **arguments) {
    if (count < 2) {
        fprintf(stderr, "usage: route_tool GRAPH < pairs (from to per line)\n");
        return 2;
    }
    route_graph graph;
    clock_t started = clock();
    if (!graph.load(fopen(arguments[1], "rb"))) {
        fprintf(stderr, "can't load %s\n", arguments[1]);
        return 1;
    }
    fprintf(stderr, "loaded %u nodes in %.0f ms\n", graph.nodes(), double(clock() - started) * 1000 / CLOCKS_PER_SEC);
    static uint32_t nodes[200000];
    route_result result = {nodes, 0, 200000, 0, 0};
    unsigned from;
    unsigned to;
    double total_ms = 0;
    int routes = 0;
    while (scanf("%u %u", &from, &to) == 2) {
        clock_t search = clock();
        bool found = graph.find(from, to, result);
        total_ms += double(clock() - search) * 1000 / CLOCKS_PER_SEC;
        routes++;
        if (found) {
            printf("%u %d %.1f\n", result.cost, result.count, result.length_metres);
        } else {
            printf("none\n");
        }
    }
    fprintf(stderr, "%d searches, %.2f ms each\n", routes, routes ? total_ms / routes : 0);
    return 0;
}
