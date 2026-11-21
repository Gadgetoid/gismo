#pragma once

#include <stdint.h>
#include <stdio.h>

namespace gismo {

struct route_node {
    int32_t east;
    int32_t north;
};

struct route_edge {
    uint32_t target;
    uint32_t cost;
};

struct route_result {
    uint32_t *nodes;
    int count;
    int capacity;
    uint32_t cost;
    double length_metres;
};

class route_graph {
public:
    route_graph();
    ~route_graph();

    bool load(FILE *file);
    bool loaded() const {
        return node_count > 0;
    }

    uint32_t nodes() const {
        return node_count;
    }

    const route_node &node(uint32_t index) const {
        return node_positions[index];
    }

    bool covers(double east, double north) const;
    int64_t nearest(double east, double north) const;
    bool find(uint32_t from, uint32_t to, route_result &result);

private:
    void release();

    uint32_t node_count;
    uint32_t edge_count;
    route_node lowest;
    route_node highest;
    route_node *node_positions;
    uint32_t *offsets;
    route_edge *edges;
    uint32_t *best;
    uint32_t *parent;
    uint8_t *closed;
    uint32_t *heap_nodes;
    uint32_t *heap_keys;
    uint32_t heap_capacity;
};

}
