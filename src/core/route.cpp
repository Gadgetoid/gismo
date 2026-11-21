#include "route.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace gismo {

namespace {

constexpr uint32_t no_parent = 0xFFFFFFFF;

uint32_t square_root(uint64_t value) {
    uint64_t result = 0;
    uint64_t bit = uint64_t(1) << 62;
    while (bit > value) {
        bit >>= 2;
    }
    while (bit) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return uint32_t(result);
}

uint32_t straight_line(const route_node &a, const route_node &b) {
    int64_t east = int64_t(a.east) - b.east;
    int64_t north = int64_t(a.north) - b.north;
    return square_root(uint64_t(east * east + north * north));
}

bool read_exact(FILE *file, void *target, size_t size) {
    return fread(target, 1, size, file) == size;
}

}

route_graph::route_graph() : node_count(0), edge_count(0), node_positions(nullptr), offsets(nullptr), edges(nullptr), best(nullptr), parent(nullptr), closed(nullptr), heap_nodes(nullptr), heap_keys(nullptr), heap_capacity(0) {
}

route_graph::~route_graph() {
    release();
}

void route_graph::release() {
    free(node_positions);
    free(offsets);
    free(edges);
    free(best);
    free(parent);
    free(closed);
    free(heap_nodes);
    free(heap_keys);
    node_positions = nullptr;
    offsets = nullptr;
    edges = nullptr;
    best = nullptr;
    parent = nullptr;
    closed = nullptr;
    heap_nodes = nullptr;
    heap_keys = nullptr;
    node_count = 0;
    edge_count = 0;
    heap_capacity = 0;
}

bool route_graph::load(FILE *file) {
    release();
    if (file == nullptr) {
        return false;
    }
    uint8_t header[20];
    bool ok = read_exact(file, header, sizeof(header)) && memcmp(header, "GISMORTE", 8) == 0 && (header[8] | header[9] << 8) == 1;
    uint32_t nodes_in_file = 0;
    uint32_t edges_in_file = 0;
    if (ok) {
        memcpy(&nodes_in_file, header + 12, 4);
        memcpy(&edges_in_file, header + 16, 4);
        node_positions = static_cast<route_node *>(malloc(sizeof(route_node) * nodes_in_file));
        offsets = static_cast<uint32_t *>(malloc(sizeof(uint32_t) * (nodes_in_file + 1)));
        edges = static_cast<route_edge *>(malloc(sizeof(route_edge) * edges_in_file));
        best = static_cast<uint32_t *>(malloc(sizeof(uint32_t) * nodes_in_file));
        parent = static_cast<uint32_t *>(malloc(sizeof(uint32_t) * nodes_in_file));
        closed = static_cast<uint8_t *>(malloc(nodes_in_file));
        heap_capacity = edges_in_file + 1;
        heap_nodes = static_cast<uint32_t *>(malloc(sizeof(uint32_t) * heap_capacity));
        heap_keys = static_cast<uint32_t *>(malloc(sizeof(uint32_t) * heap_capacity));
        ok = node_positions && offsets && edges && best && parent && closed && heap_nodes && heap_keys;
    }
    ok = ok && read_exact(file, node_positions, sizeof(route_node) * nodes_in_file);
    ok = ok && read_exact(file, offsets, sizeof(uint32_t) * (nodes_in_file + 1));
    ok = ok && read_exact(file, edges, sizeof(route_edge) * edges_in_file);
    fclose(file);
    if (!ok || nodes_in_file == 0 || offsets[nodes_in_file] != edges_in_file) {
        release();
        return false;
    }
    node_count = nodes_in_file;
    edge_count = edges_in_file;
    lowest = highest = node_positions[0];
    for (uint32_t index = 1; index < node_count; index++) {
        const route_node &position = node_positions[index];
        lowest.east = position.east < lowest.east ? position.east : lowest.east;
        lowest.north = position.north < lowest.north ? position.north : lowest.north;
        highest.east = position.east > highest.east ? position.east : highest.east;
        highest.north = position.north > highest.north ? position.north : highest.north;
    }
    return true;
}

bool route_graph::covers(double east, double north) const {
    return node_count > 0 && east * 10 >= lowest.east && east * 10 <= highest.east && north * 10 >= lowest.north && north * 10 <= highest.north;
}

int64_t route_graph::nearest(double east, double north) const {
    route_node target = {int32_t(floor(east * 10 + 0.5)), int32_t(floor(north * 10 + 0.5))};
    int64_t best_index = -1;
    int64_t best_distance = 0;
    for (uint32_t index = 0; index < node_count; index++) {
        if (offsets[index] == offsets[index + 1]) {
            continue;
        }
        int64_t east_offset = int64_t(node_positions[index].east) - target.east;
        int64_t north_offset = int64_t(node_positions[index].north) - target.north;
        int64_t distance = east_offset * east_offset + north_offset * north_offset;
        if (best_index < 0 || distance < best_distance) {
            best_index = index;
            best_distance = distance;
        }
    }
    return best_index;
}

bool route_graph::find(uint32_t from, uint32_t to, route_result &result) {
    result.count = 0;
    result.cost = 0;
    result.length_metres = 0;
    if (from >= node_count || to >= node_count) {
        return false;
    }
    memset(best, 0xFF, sizeof(uint32_t) * node_count);
    memset(closed, 0, node_count);
    const route_node &goal = node_positions[to];

    uint32_t heap_size = 0;
    auto push = [&](uint32_t node, uint32_t key) {
        if (heap_size >= heap_capacity) {
            return;
        }
        uint32_t position = heap_size++;
        while (position > 0) {
            uint32_t up = (position - 1) / 2;
            if (heap_keys[up] <= key) {
                break;
            }
            heap_nodes[position] = heap_nodes[up];
            heap_keys[position] = heap_keys[up];
            position = up;
        }
        heap_nodes[position] = node;
        heap_keys[position] = key;
    };
    auto pop = [&]() {
        uint32_t top = heap_nodes[0];
        heap_size--;
        uint32_t last_node = heap_nodes[heap_size];
        uint32_t last_key = heap_keys[heap_size];
        uint32_t position = 0;
        while (true) {
            uint32_t child = position * 2 + 1;
            if (child >= heap_size) {
                break;
            }
            if (child + 1 < heap_size && heap_keys[child + 1] < heap_keys[child]) {
                child++;
            }
            if (heap_keys[child] >= last_key) {
                break;
            }
            heap_nodes[position] = heap_nodes[child];
            heap_keys[position] = heap_keys[child];
            position = child;
        }
        if (heap_size > 0) {
            heap_nodes[position] = last_node;
            heap_keys[position] = last_key;
        }
        return top;
    };

    best[from] = 0;
    parent[from] = no_parent;
    push(from, straight_line(node_positions[from], goal));
    bool found = false;
    while (heap_size > 0) {
        uint32_t current = pop();
        if (closed[current]) {
            continue;
        }
        closed[current] = 1;
        if (current == to) {
            found = true;
            break;
        }
        uint32_t current_cost = best[current];
        for (uint32_t edge = offsets[current]; edge < offsets[current + 1]; edge++) {
            uint32_t next = edges[edge].target;
            uint32_t cost = current_cost + edges[edge].cost;
            if (cost < best[next]) {
                best[next] = cost;
                parent[next] = current;
                closed[next] = 0;
                push(next, cost + straight_line(node_positions[next], goal));
            }
        }
    }
    if (!found) {
        return false;
    }

    int count = 0;
    for (uint32_t node = to; node != no_parent; node = parent[node]) {
        count++;
    }
    if (count > result.capacity) {
        return false;
    }
    int position = count;
    for (uint32_t node = to; node != no_parent; node = parent[node]) {
        result.nodes[--position] = node;
    }
    result.count = count;
    result.cost = best[to];
    for (int index = 1; index < count; index++) {
        result.length_metres += straight_line(node_positions[result.nodes[index - 1]], node_positions[result.nodes[index]]) / 10.0;
    }
    return true;
}

}
