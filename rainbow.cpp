#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <parlay/primitives.h>
#include <parlay/random.h>
#include <parlay/sequence.h>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

struct Edge {
  int u, v;
  int color;
  int priority;

  Edge() : u(0), v(0), color(0), priority(0) {
  }
  Edge(int _u, int _v, int _c, int _p) : u(_u), v(_v), color(_c), priority(_p) {
  }
} __attribute__((packed));

parlay::sequence<Edge> generate_edge_colored_graph(int n, int num_colors) {
  int m = n * n;

  parlay::random_generator gen;
  std::uniform_int_distribution<int> vertex_dist(0, n - 1);
  std::uniform_int_distribution<int> color_dist(1, num_colors);

  parlay::sequence<Edge> edges(m);

  parlay::parallel_for(0, m, [&](size_t i) {
    auto r = gen[i];
    int u = vertex_dist(r);
    int v = vertex_dist(r);
    while (u == v) {
      v = vertex_dist(r);
    }
    int c = color_dist(r);
    edges[i] = Edge(u, v, c, i);
  });

  return edges;
}

pair<parlay::sequence<Edge>, int>
read_edge_colored_graph(const string &filename, int &n, int &m) {
  ifstream file(filename);
  if (!file.is_open()) {
    cerr << "Error: Cannot open file " << filename << endl;
    exit(1);
  }

  vector<Edge> edges_vec;
  int num_colors = 0;
  n = 0;
  m = 0;

  string line;
  while (getline(file, line)) {
    // Skip empty lines
    if (line.empty())
      continue;

    // Parse metadata from comments
    if (line[0] == '#') {
      if (line.find("Nodes:") != string::npos) {
        size_t pos = line.find("Nodes:");
        sscanf(line.c_str() + pos, "Nodes: %d", &n);
      }
      if (line.find("Edges:") != string::npos) {
        size_t pos = line.find("Edges:");
        sscanf(line.c_str() + pos, "Edges: %d", &m);
      }
      if (line.find("DistinctColors:") != string::npos) {
        size_t pos = line.find("DistinctColors:");
        sscanf(line.c_str() + pos, "DistinctColors: %d", &num_colors);
      }
      continue;
    }

    // Parse edge data
    istringstream iss(line);
    int u, v, c;
    if (iss >> u >> v >> c) {
      edges_vec.push_back(Edge(u, v, c, edges_vec.size()));
      n = max(n, max(u, v) + 1);
    }
  }

  file.close();

  m = edges_vec.size();

  // Convert to parlay::sequence
  parlay::sequence<Edge> edges(m);
  parlay::parallel_for(0, m, [&](size_t i) { edges[i] = edges_vec[i]; });

  return make_pair(edges, num_colors);
}

parlay::sequence<Edge> parallel_rainbow_matching(parlay::sequence<Edge> &edges,
                                                 int num_colors, int delta) {
  size_t m = edges.size();
  if (m == 0)
    return parlay::sequence<Edge>();

  // M[c] = edge priority matched with color c (0 if none)
  parlay::sequence<std::atomic<int>> M(num_colors + 1);
  parlay::parallel_for(0, (size_t)(num_colors + 1),
                       [&](size_t i) { M[i].store(0); });

  // Find the maximum vertex id so we know how large to make our arrays
  int n = 1 + parlay::reduce(
                  parlay::delayed_seq<int>(
                      m, [&](size_t i) { return max(edges[i].u, edges[i].v); }),
                  parlay::maximum<int>());

  parlay::sequence<std::atomic<int>> vertex_matched(n);
  parlay::parallel_for(0, (size_t)n,
                       [&](size_t i) { vertex_matched[i].store(0); });

  // Pre-allocate working arrays once and reuse them across rounds
  parlay::sequence<int> edge_status(
      delta); // 0 = removed, >0 = active (stores priority+1)
  parlay::sequence<int> T(delta); // Candidate edges to add

  // For building the adjacency lists within each prefix
  parlay::sequence<std::atomic<int>> vertex_edge_count(n);
  parlay::sequence<parlay::sequence<int>> vertex_to_edges(n);

  // Process in prefixes
  size_t num_prefixes = (m + delta - 1) / delta;

  for (size_t round = 0; round < num_prefixes; round++) {
    size_t prefix_start = round * delta;
    size_t prefix_end = min(prefix_start + (size_t)delta, m);
    size_t prefix_size = prefix_end - prefix_start;

    if (prefix_size == 0)
      break;

    // Initialize edge status - we store priority+1 so that 0 can mean "removed"
    parlay::parallel_for(0, prefix_size, [&](size_t t) {
      edge_status[t] =
          edges[prefix_start + t].priority + 1; // +1 so 0 means removed
    });

    // Build adjacency lists for this prefix so we can quickly find neighboring
    // edges First, reset vertex counts
    parlay::parallel_for(0, (size_t)n,
                         [&](size_t i) { vertex_edge_count[i].store(0); });

    // Count edges per vertex
    parlay::parallel_for(0, prefix_size, [&](size_t t) {
      if (edge_status[t] == 0)
        return;
      const Edge &e = edges[prefix_start + t];
      vertex_edge_count[e.u].fetch_add(1, std::memory_order_relaxed);
      vertex_edge_count[e.v].fetch_add(1, std::memory_order_relaxed);
    });

    // Allocate space for each vertex's edge list
    parlay::parallel_for(0, (size_t)n, [&](size_t v) {
      int count = vertex_edge_count[v].load();
      if (count > 0) {
        vertex_to_edges[v] = parlay::sequence<int>(count);
        vertex_edge_count[v].store(0); // Reset for use as index
      } else {
        vertex_to_edges[v] = parlay::sequence<int>();
      }
    });

    // Populate adjacency lists
    parlay::parallel_for(0, prefix_size, [&](size_t t) {
      if (edge_status[t] == 0)
        return;
      const Edge &e = edges[prefix_start + t];
      int idx_u =
          vertex_edge_count[e.u].fetch_add(1, std::memory_order_relaxed);
      int idx_v =
          vertex_edge_count[e.v].fetch_add(1, std::memory_order_relaxed);
      if (idx_u < (int)vertex_to_edges[e.u].size())
        vertex_to_edges[e.u][idx_u] = t;
      if (idx_v < (int)vertex_to_edges[e.v].size())
        vertex_to_edges[e.v][idx_v] = t;
    });

    int iterations = 0;

    // Check if there are any active edges left to process
    bool has_edges = parlay::reduce(
        parlay::delayed_seq<bool>(
            prefix_size, [&](size_t t) { return edge_status[t] != 0; }),
        parlay::binary_op([](bool a, bool b) { return a || b; }, false));

    while (has_edges && iterations < 100) {
      iterations++;

      // Reset T array
      parlay::parallel_for(0, prefix_size, [&](size_t t) { T[t] = 0; });

      // Phase 1: Find edges with no higher-priority neighbors (candidates to
      // add)
      parlay::parallel_for(0, prefix_size, [&](size_t t) {
        if (edge_status[t] == 0)
          return;

        const Edge &e = edges[prefix_start + t];
        int e_priority = e.priority;

        // Skip if this color is already taken
        if (M[e.color].load(std::memory_order_relaxed) != 0)
          return;

        // Skip if either endpoint is already matched
        if (vertex_matched[e.u].load(std::memory_order_relaxed) != 0 ||
            vertex_matched[e.v].load(std::memory_order_relaxed) != 0)
          return;

        // Check if any neighboring edge has higher priority
        bool can_add = true;

        // Check edges incident on e.u
        for (size_t i = 0; i < vertex_to_edges[e.u].size() && can_add; i++) {
          int j = vertex_to_edges[e.u][i];
          if (j != (int)t && edge_status[j] != 0) {
            int other_priority = edges[prefix_start + j].priority;
            if (other_priority < e_priority) {
              can_add = false;
            }
          }
        }

        // Check edges incident on e.v
        for (size_t i = 0; i < vertex_to_edges[e.v].size() && can_add; i++) {
          int j = vertex_to_edges[e.v][i];
          if (j != (int)t && edge_status[j] != 0) {
            int other_priority = edges[prefix_start + j].priority;
            if (other_priority < e_priority) {
              can_add = false;
            }
          }
        }

        if (can_add) {
          T[t] = e_priority + 1;
        }
      });

      // Phase 2: Add edges to matching
      parlay::parallel_for(0, prefix_size, [&](size_t t) {
        if (T[t] != 0) {
          const Edge &e = edges[prefix_start + t];

          // Try to claim both vertices and the color atomically
          int expected_u = 0;
          int expected_v = 0;
          int expected_color = 0;

          if (vertex_matched[e.u].compare_exchange_strong(expected_u, T[t]) &&
              vertex_matched[e.v].compare_exchange_strong(expected_v, T[t])) {
            if (M[e.color].compare_exchange_strong(expected_color, T[t])) {
              edge_status[t] = 0; // Successfully matched
            } else {
              // Color was taken, release vertices
              vertex_matched[e.u].store(0);
              vertex_matched[e.v].store(0);
            }
          } else {
            // Vertex was taken, release if we claimed e.u
            if (expected_u == 0)
              vertex_matched[e.u].store(0);
          }
        }
      });

      // Phase 3: Remove conflicting edges
      parlay::parallel_for(0, prefix_size, [&](size_t t) {
        if (edge_status[t] == 0)
          return;

        const Edge &e = edges[prefix_start + t];

        if (M[e.color].load(std::memory_order_relaxed) != 0 ||
            vertex_matched[e.u].load(std::memory_order_relaxed) != 0 ||
            vertex_matched[e.v].load(std::memory_order_relaxed) != 0) {
          edge_status[t] = 0;
        }
      });

      // Check if any edges remain for next iteration
      has_edges = parlay::reduce(
          parlay::delayed_seq<bool>(
              prefix_size, [&](size_t t) { return edge_status[t] != 0; }),
          parlay::binary_op([](bool a, bool b) { return a || b; }, false));
    }
  }

  // Collect all the matched edges into the final result
  auto matched_colors =
      parlay::filter(parlay::iota<int>(num_colors + 1),
                     [&](int c) { return c > 0 && M[c].load() != 0; });

  parlay::sequence<Edge> matching(matched_colors.size());
  parlay::parallel_for(0, matched_colors.size(), [&](size_t i) {
    int c = matched_colors[i];
    int priority = M[c].load() - 1;
    matching[i] = edges[priority];
  });

  return matching;
}

int main(int argc, char *argv[]) {
  if (argc < 2 || argc > 4) {
    cout << "Usage: " << argv[0] << " <graph_file> [delta] [--skip-validation]"
         << endl;
    cout << "Example: " << argv[0] << " colored_graphs/Youtube_colors_40pct.txt"
         << endl;
    cout << "         " << argv[0]
         << " colored_graphs/Youtube_colors_40pct.txt 5000" << endl;
    cout << "         " << argv[0]
         << " colored_graphs/Youtube_colors_40pct.txt 5000 --skip-validation"
         << endl;
    return 1;
  }

  string filename = argv[1];
  bool skip_validation = false;

  // Check for --skip-validation flag
  for (int i = 2; i < argc; i++) {
    if (string(argv[i]) == "--skip-validation") {
      skip_validation = true;
    }
  }

  cout << "Parallel Rainbow Matching (PG-MRM)\n";
  cout << "Graph file: " << filename << "\n\n";

  // Read graph from file
  int n, m;
  auto [edges, num_colors] = read_edge_colored_graph(filename, n, m);

  cout << "Vertices: " << n << ", Edges: " << m << ", Colors: " << num_colors
       << "\n\n";

  // Random shuffle
  edges = parlay::random_shuffle(edges);
  parlay::parallel_for(0, m, [&](size_t i) { edges[i].priority = i; });

  // Set prefix size
  int delta;
  bool delta_specified = false;

  // Check if delta is specified (and it's not the --skip-validation flag)
  if (argc >= 3 && string(argv[2]) != "--skip-validation") {
    delta = atoi(argv[2]);
    if (delta <= 0) {
      cout << "Error: delta must be positive" << endl;
      return 1;
    }
    delta_specified = true;
    cout << "Prefix size: " << delta << " (user specified)\n";
  } else {
    delta = max(1000, m / 100);
    cout << "Prefix size: " << delta << " (auto)\n";
  }

  if (skip_validation) {
    cout << "Validation: SKIPPED\n";
  }
  cout << "\n";

  // Run algorithm
  auto start = chrono::high_resolution_clock::now();
  auto matching = parallel_rainbow_matching(edges, num_colors, delta);
  auto end = chrono::high_resolution_clock::now();

  auto duration_ms = chrono::duration_cast<chrono::milliseconds>(end - start);
  double duration_s = duration_ms.count() / 1000.0;

  cout << "\n=== Results ===\n";
  cout << "Matching size: " << matching.size() << endl;
  cout << "Time: " << duration_ms.count() << " ms (" << duration_s << " s)\n";

  // Verification (skip if requested)
  if (skip_validation) {
    cout << "\n=== Verification SKIPPED ===\n";
    return 0;
  }

  cout << "\n=== Verification ===\n";

  bool valid = true;

  // Check distinct colors
  cout << "Checking distinct colors...\n";
  parlay::sequence<int> color_check(num_colors + 1, 0);
  for (const auto &e : matching) {
    if (color_check[e.color] != 0) {
      valid = false;
      cout << "ERROR: Color " << e.color << " appears twice\n";
      break;
    }
    color_check[e.color] = 1;
  }
  if (valid) {
    cout << "OK: All colors distinct\n";
  }

  // Check no shared endpoints
  cout << "Checking no shared endpoints...\n";
  parlay::sequence<int> vertex_check(n, 0);
  for (size_t i = 0; i < matching.size() && valid; i++) {
    const Edge &e = matching[i];

    if (vertex_check[e.u] != 0) {
      valid = false;
      cout << "ERROR: Vertex " << e.u << " in multiple edges\n";
      break;
    }
    if (vertex_check[e.v] != 0) {
      valid = false;
      cout << "ERROR: Vertex " << e.v << " in multiple edges\n";
      break;
    }

    vertex_check[e.u] = e.color;
    vertex_check[e.v] = e.color;
  }
  if (valid) {
    cout << "OK: No shared endpoints\n";
  }

  // Check edges are valid
  cout << "Checking edge validity...\n";
  for (size_t i = 0; i < matching.size() && valid; i++) {
    const Edge &e = matching[i];
    if (e.priority >= edges.size()) {
      valid = false;
      cout << "ERROR: Invalid priority\n";
      break;
    }
    const Edge &original = edges[e.priority];
    if (e.u != original.u || e.v != original.v || e.color != original.color) {
      valid = false;
      cout << "ERROR: Edge doesn't match original\n";
      break;
    }
  }
  if (valid) {
    cout << "OK: All edges valid\n";
  }

  // Check maximality - O(m) instead of O(m × matching_size)
  // Note: No need to check if edge is in matching because if it is,
  // its vertices would already be marked (not free), so it won't be counted
  // cout << "Checking maximality...\n";
  // int num_addable = 0;

  // for (size_t i = 0; i < edges.size() && valid; i++) {
  //   const Edge &e = edges[i];

  //   bool u_free = (vertex_check[e.u] == 0);
  //   bool v_free = (vertex_check[e.v] == 0);
  //   bool color_free = (color_check[e.color] == 0);

  //   if (u_free && v_free && color_free) {
  //     num_addable++;
  //   }
  // }

  // if (num_addable > 0) {
  //   cout << "WARNING: " << num_addable
  //        << " edges could be added (not maximal)\n";
  // } else {
  //   cout << "OK: Maximal matching\n";
  // }

  cout << "\n";
  if (valid) {
    cout << "RESULT: Valid rainbow matching\n";
  } else {
    cout << "RESULT: Invalid matching\n";
  }

  return 0;
}
