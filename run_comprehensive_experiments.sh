#!/bin/bash

# Comprehensive experimental script for rainbow matching
# Runs each configuration 5 times and generates detailed statistics

set -e  # Exit on error

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

# Configuration
CORE_COUNTS=(1 2 4 8 16 32 64)
GRAPH_DIR="colored_graphs"
RESULTS_DIR="results"
NUM_RUNS=5

# Delta configurations to test (as divisors of m)
DELTA_CONFIGS=("auto" "1000" "500" "200" "100" "50" "20" "10" "5" "2" "1")

echo -e "${CYAN}========================================================${NC}"
echo -e "${CYAN}  Rainbow Matching Comprehensive Experiments${NC}"
echo -e "${CYAN}  Testing: ${#CORE_COUNTS[@]} core configs x ${#DELTA_CONFIGS[@]} delta configs x $NUM_RUNS runs${NC}"
echo -e "${CYAN}========================================================${NC}"
echo ""

# Clean and create results directory
echo -e "${BLUE}Cleaning results directory...${NC}"
rm -rf "$RESULTS_DIR"
mkdir -p "$RESULTS_DIR"
echo -e "${GREEN}Results directory cleaned${NC}\n"

# Compile
echo -e "${BLUE}Compiling rainbow.cpp in RELEASE mode...${NC}"
rm -rf build
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
if [ $? -ne 0 ]; then
    echo -e "${RED}CMake configuration failed!${NC}"
    exit 1
fi
cmake --build build
if [ $? -ne 0 ]; then
    echo -e "${RED}Compilation failed!${NC}"
    exit 1
fi
echo -e "${GREEN}Compilation successful${NC}\n"

# Get all graph files
GRAPH_FILES=("$GRAPH_DIR"/*.txt)

# Check if any graph files exist
if [ ${#GRAPH_FILES[@]} -eq 0 ] || [ ! -f "${GRAPH_FILES[0]}" ]; then
    echo -e "${RED}No graph files found in $GRAPH_DIR${NC}"
    exit 1
fi

echo -e "${BLUE}Found ${#GRAPH_FILES[@]} graph file(s) to process${NC}\n"

# Create CSV files with headers
ALL_RUNS_CSV="$RESULTS_DIR/all_runs.csv"
AVERAGES_CSV="$RESULTS_DIR/averages.csv"

echo "GraphName,NumVertices,NumEdges,NumColors,Cores,DeltaConfig,DeltaValue,Run,MatchingSize,Time_ms,Time_s,Valid,Maximal" > "$ALL_RUNS_CSV"
echo "GraphName,NumVertices,NumEdges,NumColors,Cores,DeltaConfig,DeltaValue,AvgMatchingSize,AvgTime_ms,AvgTime_s,StdDevTime_ms,MinTime_ms,MaxTime_ms,SuccessRate" > "$AVERAGES_CSV"

# Temporary file for storing run data
TEMP_DATA="/tmp/rainbow_runs_$$.txt"

# Counter for progress
total_configs=0
completed_configs=0

# Calculate total number of configurations
for graph_file in "${GRAPH_FILES[@]}"; do
    if [ ! -f "$graph_file" ]; then continue; fi
    total_configs=$((total_configs + ${#CORE_COUNTS[@]} * ${#DELTA_CONFIGS[@]}))
done

echo -e "${YELLOW}Total configurations to test: $total_configs${NC}"
echo -e "${YELLOW}Total runs to execute: $((total_configs * NUM_RUNS))${NC}\n"

# Function to extract value from output
extract_value() {
    local file=$1
    local pattern=$2
    local field=$3
    grep "$pattern" "$file" 2>/dev/null | awk "{print \$$field}" | head -1
}

# Function to calculate statistics
calculate_stats() {
    local values="$1"
    local count=$(echo "$values" | wc -l)

    if [ $count -eq 0 ]; then
        echo "0 0 0 0 0"
        return
    fi

    echo "$values" | awk '{
        sum += $1
        sumsq += $1 * $1
        if (NR == 1) { min = $1; max = $1 }
        if ($1 < min) min = $1
        if ($1 > max) max = $1
    }
    END {
        avg = sum / NR
        stddev = sqrt(sumsq / NR - avg * avg)
        printf "%.2f %.2f %.2f %.2f %d\n", avg, stddev, min, max, NR
    }'
}

# Process each graph file
for graph_file in "${GRAPH_FILES[@]}"; do
    if [ ! -f "$graph_file" ]; then continue; fi

    graph_name=$(basename "$graph_file" .txt)

    echo -e "${CYAN}========================================================${NC}"
    echo -e "${CYAN}Processing: ${YELLOW}$graph_name${NC}"
    echo -e "${CYAN}========================================================${NC}"

    # Extract graph metadata
    n_vertices=$(grep "# Nodes:" "$graph_file" | awk '{print $3}')
    m_edges=$(grep "# Nodes:" "$graph_file" | awk '{print $5}')
    num_colors=$(grep "DistinctColors:" "$graph_file" | awk '{print $3}')

    echo -e "  Vertices: ${GREEN}$n_vertices${NC}, Edges: ${GREEN}$m_edges${NC}, Colors: ${GREEN}$num_colors${NC}\n"

    # Test each core configuration
    for cores in "${CORE_COUNTS[@]}"; do
        echo -e "  ${BLUE}[Cores: $cores]${NC}"

        # Test each delta configuration
        for delta_config in "${DELTA_CONFIGS[@]}"; do
            completed_configs=$((completed_configs + 1))
            progress=$((completed_configs * 100 / total_configs))

            echo -ne "    Delta: ${YELLOW}$delta_config${NC} - Running: "

            # Clear temp data file
            > "$TEMP_DATA"

            # Determine actual delta value
            if [ "$delta_config" = "auto" ]; then
                delta_arg=""
                delta_value_for_csv="auto"
            else
                delta_value=$((m_edges / delta_config))
                if [ $delta_value -eq 0 ]; then
                    delta_value=1
                fi
                delta_arg="$delta_value"
                delta_value_for_csv="$delta_value"
            fi

            success_count=0
            first_run_valid="UNKNOWN"

            # Run NUM_RUNS times
            for run in $(seq 1 $NUM_RUNS); do
                echo -n "$run."

                output_file="$RESULTS_DIR/tmp_${graph_name}_c${cores}_d${delta_config}_r${run}.txt"

                # Run 1: with validation; Runs 2+: skip validation if first passed
                if [ $run -eq 1 ]; then
                    if [ "$delta_config" = "auto" ]; then
                        PARLAY_NUM_THREADS=$cores timeout 10800 ./build/rainbow "$graph_file" > "$output_file" 2>&1
                    else
                        PARLAY_NUM_THREADS=$cores timeout 10800 ./build/rainbow "$graph_file" $delta_arg > "$output_file" 2>&1
                    fi
                else
                    if [ "$first_run_valid" = "YES" ]; then
                        if [ "$delta_config" = "auto" ]; then
                            PARLAY_NUM_THREADS=$cores timeout 10800 ./build/rainbow "$graph_file" --skip-validation > "$output_file" 2>&1
                        else
                            PARLAY_NUM_THREADS=$cores timeout 10800 ./build/rainbow "$graph_file" $delta_arg --skip-validation > "$output_file" 2>&1
                        fi
                    else
                        if [ "$delta_config" = "auto" ]; then
                            PARLAY_NUM_THREADS=$cores timeout 10800 ./build/rainbow "$graph_file" > "$output_file" 2>&1
                        else
                            PARLAY_NUM_THREADS=$cores timeout 10800 ./build/rainbow "$graph_file" $delta_arg > "$output_file" 2>&1
                        fi
                    fi
                fi

                exit_code=$?

                if [ $exit_code -eq 0 ]; then
                    matching_size=$(extract_value "$output_file" "Matching size:" 3)
                    time_ms=$(extract_value "$output_file" "Time:" 2)
                    time_s=$(extract_value "$output_file" "Time:" 4 | tr -d '()')

                    if [ "$delta_config" = "auto" ]; then
                        actual_delta=$(extract_value "$output_file" "Prefix size:" 3)
                        delta_value_for_csv="$actual_delta"
                    fi

                    if [ $run -eq 1 ]; then
                        if grep -q "RESULT: Valid rainbow matching" "$output_file"; then
                            valid="YES"
                            first_run_valid="YES"
                        else
                            valid="NO"
                            first_run_valid="NO"
                        fi
                    else
                        valid="$first_run_valid"
                    fi
                    maximal="N/A"

                    echo "$graph_name,$n_vertices,$m_edges,$num_colors,$cores,$delta_config,$delta_value_for_csv,$run,$matching_size,$time_ms,$time_s,$valid,$maximal" >> "$ALL_RUNS_CSV"

                    echo "$time_ms" >> "$TEMP_DATA"
                    success_count=$((success_count + 1))

                elif [ $exit_code -eq 124 ]; then
                    echo "$graph_name,$n_vertices,$m_edges,$num_colors,$cores,$delta_config,$delta_value,$run,TIMEOUT,TIMEOUT,TIMEOUT,NO,NO" >> "$ALL_RUNS_CSV"
                else
                    if grep -q "bad_alloc" "$output_file" 2>/dev/null; then
                        echo "$graph_name,$n_vertices,$m_edges,$num_colors,$cores,$delta_config,$delta_value,$run,OOM,OOM,OOM,NO,NO" >> "$ALL_RUNS_CSV"
                    else
                        echo "$graph_name,$n_vertices,$m_edges,$num_colors,$cores,$delta_config,$delta_value,$run,FAILED,FAILED,FAILED,NO,NO" >> "$ALL_RUNS_CSV"
                    fi
                fi

                rm -f "$output_file"
            done

            # Calculate statistics
            if [ $success_count -gt 0 ]; then
                times=$(cat "$TEMP_DATA")
                stats=$(calculate_stats "$times")

                avg_time=$(echo "$stats" | awk '{print $1}')
                stddev_time=$(echo "$stats" | awk '{print $2}')
                min_time=$(echo "$stats" | awk '{print $3}')
                max_time=$(echo "$stats" | awk '{print $4}')

                avg_matching=$(grep "$graph_name,$n_vertices,$m_edges,$num_colors,$cores,$delta_config," "$ALL_RUNS_CSV" | \
                              grep -v "TIMEOUT\|OOM\|FAILED" | head -1 | cut -d',' -f9)

                avg_time_s=$(echo "scale=3; $avg_time / 1000" | bc)
                success_rate=$(echo "scale=2; $success_count * 100 / $NUM_RUNS" | bc)

                final_delta=$(grep "$graph_name,$n_vertices,$m_edges,$num_colors,$cores,$delta_config," "$ALL_RUNS_CSV" | \
                             grep -v "TIMEOUT\|OOM\|FAILED" | head -1 | cut -d',' -f7)

                echo "$graph_name,$n_vertices,$m_edges,$num_colors,$cores,$delta_config,$final_delta,$avg_matching,$avg_time,$avg_time_s,$stddev_time,$min_time,$max_time,$success_rate%" >> "$AVERAGES_CSV"

                echo -e " ${GREEN}OK${NC} [${progress}%] Avg: ${avg_time}ms (std: ${stddev_time}ms)"
            else
                echo "$graph_name,$n_vertices,$m_edges,$num_colors,$cores,$delta_config,FAILED,FAILED,FAILED,FAILED,FAILED,FAILED,FAILED,0%" >> "$AVERAGES_CSV"
                echo -e " ${RED}FAIL${NC} [${progress}%] All runs failed"
            fi
        done
    done
    echo ""
done

# Clean up temp file
rm -f "$TEMP_DATA"

echo -e "${CYAN}========================================================${NC}"
echo -e "${CYAN}  EXPERIMENTS COMPLETED${NC}"
echo -e "${CYAN}========================================================${NC}"
echo ""
echo -e "${GREEN}Results saved:${NC}"
echo -e "  All runs:  ${YELLOW}$ALL_RUNS_CSV${NC}"
echo -e "  Averages:  ${YELLOW}$AVERAGES_CSV${NC}"
echo ""

# Show preview of averages
echo -e "${YELLOW}Preview of averages (first 10 rows):${NC}"
head -11 "$AVERAGES_CSV" | column -t -s','

echo ""
echo -e "${GREEN}Done!${NC}"
