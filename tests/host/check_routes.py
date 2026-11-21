# /// script
# requires-python = ">=3.10"
# dependencies = ["numpy", "pillow", "pyproj"]
# ///

import heapq
import random
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from make_routes import read_graph


def dijkstra(offsets, edges, start, goal):
    best = {start: 0}
    queue = [(0, start)]
    done = set()
    while queue:
        cost, node = heapq.heappop(queue)
        if node in done:
            continue
        if node == goal:
            return cost
        done.add(node)
        for index in range(offsets[node], offsets[node + 1]):
            target, step = edges[index]
            candidate = cost + step
            if candidate < best.get(target, 1 << 62):
                best[target] = candidate
                heapq.heappush(queue, (candidate, target))
    return None


def main():
    tool, graph_path = sys.argv[1], sys.argv[2]
    nodes, offsets, edges = read_graph(graph_path)
    connected = [index for index in range(len(nodes)) if offsets[index + 1] > offsets[index]]
    generator = random.Random(3)
    centre = (sorted(nodes[index][0] for index in connected)[len(connected) // 2], sorted(nodes[index][1] for index in connected)[len(connected) // 2])
    nearby = [index for index in connected if abs(nodes[index][0] - centre[0]) < 15000 and abs(nodes[index][1] - centre[1]) < 15000]
    pairs = [(generator.choice(nearby), generator.choice(nearby)) for _ in range(25)]
    pairs += [(generator.choice(connected), generator.choice(connected)) for _ in range(5)]
    result = subprocess.run([tool, graph_path], input="".join(f"{a} {b}\n" for a, b in pairs), capture_output=True, text=True, check=True)
    print(result.stderr.strip())
    ok = True
    found = 0
    for (start, goal), line in zip(pairs, result.stdout.split("\n")):
        expected = dijkstra(offsets, edges, start, goal)
        if line == "none":
            ok = ok and expected is None
            continue
        cost = int(line.split()[0])
        found += 1
        if cost != expected:
            print(f"MISMATCH {start} -> {goal}: A* {cost}, Dijkstra {expected}")
            ok = False
    print(f"{found} of {len(pairs)} routes found, costs " + ("match Dijkstra" if ok else "DIFFER"))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
