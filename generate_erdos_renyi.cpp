#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

using namespace std;

// True Erdos-Renyi G(n,p) generator
// Each edge (i,j) exists independently with probability p
void generate_erdos_renyi_colored(
    int n,
    double p,
    double color_pct,
    const string& output_file,
    const string& graph_name
) {
    cout << "Generating " << graph_name << "..." << endl;

    auto start = chrono::high_resolution_clock::now();

    long long max_possible = (long long)n * (n - 1) / 2;
    long long expected_edges = (long long)(max_possible * p);

    cout << "  n = " << n << ", p = " << p << endl;
    cout << "  Max possible edges: " << max_possible << endl;
    cout << "  Expected edges (n(n-1)/2 * p): " << expected_edges << endl;

    // Random generator with fixed seed for reproducibility
    mt19937_64 gen(42 + (int)(p * 1000) + (int)(color_pct * 100));
    uniform_real_distribution<double> dist(0.0, 1.0);

    // Phase 1 & 2 combined: Generate edges directly
    cout << "  Generating edges..." << endl;

    vector<pair<int, int>> edges;
    edges.reserve(expected_edges);  // Reserve expected size to reduce reallocations

    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if (dist(gen) < p) {  // TRUE G(n,p): edge exists with probability p
                edges.push_back({i + 1, j + 1});  // 1-indexed vertices
            }
        }

        // Progress indicator
        if (i > 0 && i % 1000 == 0) {
            cout << "    Processed vertex " << i << "/" << n
                 << " (" << edges.size() << " edges so far)" << endl;
        }
    }

    long long total_edges = edges.size();
    cout << "  Actual edges generated: " << total_edges << endl;
    cout << "  Ratio actual/expected: " << (double)total_edges / expected_edges << endl;

    // Phase 3: Assign colors
    cout << "  Assigning colors..." << endl;

    int num_distinct_colors = (int)(total_edges * color_pct);
    if (num_distinct_colors == 0) num_distinct_colors = 1;

    cout << "  Distinct colors: " << num_distinct_colors
         << " (" << (int)(color_pct * 100) << "% of edges)" << endl;

    // Create shuffled indices to randomize which edges get distinct colors
    vector<long long> shuffled_indices(total_edges);
    iota(shuffled_indices.begin(), shuffled_indices.end(), 0);
    shuffle(shuffled_indices.begin(), shuffled_indices.end(), gen);

    // Create color array
    vector<int> colors(total_edges);

    for (long long i = 0; i < total_edges; i++) {
        long long idx = shuffled_indices[i];
        if (i < num_distinct_colors) {
            // Distinct color (1-indexed)
            colors[idx] = i + 1;
        } else {
            // Reuse a color from the distinct pool
            colors[idx] = (i % num_distinct_colors) + 1;
        }
    }

    auto gen_end = chrono::high_resolution_clock::now();
    auto gen_time = chrono::duration_cast<chrono::seconds>(gen_end - start).count();
    cout << "  Generation time: " << gen_time << " seconds" << endl;

    // Phase 4: Write to file
    cout << "  Writing to " << output_file << "..." << endl;

    ofstream file(output_file);
    if (!file.is_open()) {
        cerr << "Error: Cannot open file " << output_file << endl;
        return;
    }

    // Write header
    file << "# Undirected graph: Erdos-Renyi G(n,p)\n";
    file << "# " << graph_name << "\n";
    file << "# Nodes: " << n << " Edges: " << total_edges << "\n";
    file << "# Edge-colored version (True G(n,p) model with p=" << p << ")\n";
    file << "# DistinctColors: " << num_distinct_colors
         << " (" << (int)(color_pct * 100) << "% of edges)\n";
    file << "# FromNodeId\tToNodeId\tColor\n";

    // Write edges with buffered output for speed
    const size_t BUFFER_SIZE = 1000000;
    string buffer;
    buffer.reserve(BUFFER_SIZE * 30);

    for (long long i = 0; i < total_edges; i++) {
        buffer += to_string(edges[i].first) + "\t" +
                  to_string(edges[i].second) + "\t" +
                  to_string(colors[i]) + "\n";

        if (buffer.size() >= BUFFER_SIZE * 25) {
            file << buffer;
            buffer.clear();
        }

        // Progress indicator
        if (i > 0 && i % 10000000 == 0) {
            cout << "    Written " << i / 1000000 << "M edges..." << endl;
        }
    }

    if (!buffer.empty()) {
        file << buffer;
    }

    file.close();

    auto end = chrono::high_resolution_clock::now();
    auto total_time = chrono::duration_cast<chrono::seconds>(end - start).count();
    cout << "  Total time: " << total_time << " seconds" << endl;
    cout << "  Done!" << endl << endl;
}

int main(int argc, char* argv[]) {
    cout << "=== Erdos-Renyi G(n,p) Colored Graph Generator ===" << endl;
    cout << "=== Using TRUE probability distribution ===" << endl << endl;

    // Target: ~100 million edges expected
    // For G(n,p): expected_edges = n(n-1)/2 * p
    // So: n ≈ sqrt(2 * expected_edges / p)
    long long target_expected_edges = 100000000;

    // Edge densities (probability p in G(n,p))
    vector<double> densities = {0.4, 0.6, 0.8};

    // Color percentages
    vector<double> color_pcts = {0.20, 0.40, 0.80};

    // Output directory
    string output_dir = "colored_graphs/";

    cout << "Target expected edges: " << target_expected_edges << " (100 million)" << endl;
    cout << "Densities (p): 0.2, 0.4, 0.6, 0.8" << endl;
    cout << "Color percentages: 20%, 40%, 80%" << endl;
    cout << "Total graphs to generate: " << densities.size() * color_pcts.size() << endl;
    cout << endl;

    cout << "NOTE: Actual edge count will vary due to probabilistic generation!" << endl;
    cout << "WARNING: This will generate ~24-36 GB of graph files!" << endl;
    cout << endl;

    for (double p : densities) {
        // Calculate n to get approximately target expected edges
        // expected = n(n-1)/2 * p ≈ n^2 * p / 2
        // n = sqrt(2 * expected / p)
        int n = (int)ceil(sqrt(2.0 * target_expected_edges / p));

        int density_pct = (int)(p * 100);
        cout << "=== Density p = " << p << " (" << density_pct << "%) ===" << endl;
        cout << "Computed n = " << n << endl;
        cout << "Expected edges ≈ " << (long long)((long long)n * (n-1) / 2 * p) << endl;
        cout << endl;

        for (double color_pct : color_pcts) {
            int color_pct_int = (int)(color_pct * 100);

            string graph_name = "erdos_renyi_p" + to_string(density_pct) +
                               "_colors_" + to_string(color_pct_int) + "pct";
            string output_file = output_dir + graph_name + ".txt";

            generate_erdos_renyi_colored(n, p, color_pct, output_file, graph_name);
        }
    }

    cout << "=== All graphs generated! ===" << endl;

    return 0;
}
