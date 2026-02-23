# PG-MRM: Parallel Greedy Algorithm for Maximal Rainbow Matching

A work-efficient parallel algorithm for computing maximal rainbow matchings on edge-colored graphs, implemented in C++17 using [ParlayLib](https://github.com/cmuparlay/parlaylib).

Given an edge-colored graph G = (V, E) with coloring function chi: E -> Q, a **maximal rainbow matching** (MRM) is a matching where every edge has a distinct color and no edge can be added without violating the matching or rainbow property.

## Algorithm

The algorithm (PG-MRM) processes edges in **delta-prefixes** of a random priority ordering. Within each prefix, it iterates the following parallel phases until no active edge remains:

1. **Phase 1 (Identify ready edges):** An edge is *ready* if both endpoints are unmatched, its color is unused, and it has the highest priority among active edges at both endpoints. Readiness is checked via per-prefix adjacency lists.

2. **Phase 2 (Claim vertices and color):** Ready edges attempt to claim both endpoints and their color slot using atomic compare-and-swap operations. An edge that fails to claim any resource releases previously claimed resources.

3. **Phase 3 (Deactivate conflicts):** All remaining edges that share an endpoint or color with a newly matched edge are deactivated.

**Complexity:**
- Expected work: O(m)
- Worst-case work: O(m log^2 m)
- Depth: O(log^3 m) on CRCW PRAM
- Dependence depth under random ordering: O(log^2 m) w.h.p.

The algorithm produces the same matching as the sequential greedy algorithm for a given edge ordering, ensuring reproducibility across different numbers of processors.

## Building

Requires CMake >= 3.14 and a C++17 compiler. ParlayLib is fetched automatically.

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Input Format

Edge-colored graphs in text format (one edge per line: `u v color`). Lines starting with `#` are treated as comments and may contain metadata:

```
# Undirected graph
# Nodes: 1000 Edges: 50000
# DistinctColors: 10000
# FromNodeId ToNodeId Color
1 5 42
1 12 7
2 8 42
...
```

## Usage

```bash
# Basic usage (auto prefix size = max(1000, m/100))
./build/rainbow <graph_file>

# Specify prefix size (delta)
./build/rainbow <graph_file> <delta>

# Skip validation for benchmarking
./build/rainbow <graph_file> --skip-validation
./build/rainbow <graph_file> <delta> --skip-validation
```

### Controlling Parallelism

Set the number of threads via ParlayLib's environment variable:

```bash
PARLAY_NUM_THREADS=8 ./build/rainbow graph.txt
```

## Graph Generation

Generate Erdos-Renyi G(n,p) edge-colored graphs:

```bash
./build/generate_erdos_renyi
```

This generates graphs with varying edge densities (p = 0.4, 0.6, 0.8) and color densities (20%, 40%, 80% of edges) into the `colored_graphs/` directory.

## Running Experiments

```bash
# Basic experiments across core counts
chmod +x scripts/run_experiments.sh
scripts/run_experiments.sh

# Comprehensive experiments (multiple delta configs, 5 runs each)
chmod +x scripts/run_comprehensive_experiments.sh
scripts/run_comprehensive_experiments.sh
```

Results are saved as CSV files in the `results/` directory.

## Verification

When run without `--skip-validation`, the program verifies:
- **Distinct colors:** No two matched edges share a color
- **No shared endpoints:** No two matched edges share a vertex
- **Edge validity:** All matched edges correspond to edges in the input graph

## Project Structure

```
.
├── CMakeLists.txt                        Build configuration
├── rainbow.cpp                           PG-MRM implementation
├── generate_erdos_renyi.cpp              Erdos-Renyi graph generator
└── scripts/
    ├── run_experiments.sh                Basic experiment runner
    └── run_comprehensive_experiments.sh  Full benchmark suite
```

## License

MIT License
