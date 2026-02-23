#!/bin/bash

# Automation script to run rainbow matching on all graph instances
# with different core counts

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Core counts to test
CORE_COUNTS=(1 2 4 8 16 32 64)

# Directory containing graph files
GRAPH_DIR="colored_graphs"

# Output directory for results
RESULTS_DIR="results"
mkdir -p "$RESULTS_DIR"

# Compile the program
echo -e "${BLUE}===================================${NC}"
echo -e "${BLUE}Compiling rainbow.cpp...${NC}"
echo -e "${BLUE}===================================${NC}"
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release > /dev/null 2>&1
cmake --build build
if [ $? -ne 0 ]; then
    echo -e "${RED}Compilation failed!${NC}"
    exit 1
fi
echo -e "${GREEN}Compilation successful!${NC}\n"

# Get all graph files
GRAPH_FILES=("$GRAPH_DIR"/*.txt)

# Check if any graph files exist
if [ ${#GRAPH_FILES[@]} -eq 0 ]; then
    echo -e "${RED}No graph files found in $GRAPH_DIR${NC}"
    exit 1
fi

echo -e "${BLUE}===================================${NC}"
echo -e "${BLUE}Found ${#GRAPH_FILES[@]} graph file(s)${NC}"
echo -e "${BLUE}===================================${NC}\n"

# Create a summary file
SUMMARY_FILE="$RESULTS_DIR/summary.csv"
echo "Graph,Cores,MatchingSize,Time_ms,Time_s" > "$SUMMARY_FILE"

# Run experiments
for graph_file in "${GRAPH_FILES[@]}"; do
    # Skip if not a regular file
    if [ ! -f "$graph_file" ]; then
        continue
    fi

    # Extract graph name (filename without path and extension)
    graph_name=$(basename "$graph_file" .txt)

    echo -e "${YELLOW}===================================${NC}"
    echo -e "${YELLOW}Processing: $graph_name${NC}"
    echo -e "${YELLOW}===================================${NC}"

    for cores in "${CORE_COUNTS[@]}"; do
        echo -e "${GREEN}Running with $cores cores...${NC}"

        # Create output filename
        output_file="$RESULTS_DIR/${graph_name}_cores${cores}.txt"

        # Run the program with specified number of cores
        PARLAY_NUM_THREADS=$cores ./build/rainbow "$graph_file" > "$output_file" 2>&1

        exit_code=$?
        if [ $exit_code -eq 0 ]; then
            # Extract matching size and time from output
            matching_size=$(grep "Matching size:" "$output_file" | awk '{print $3}')
            time_ms=$(grep "Time:" "$output_file" | awk '{print $2}')
            time_s=$(grep "Time:" "$output_file" | awk '{print $4}' | tr -d '()')

            # Append to summary
            echo "$graph_name,$cores,$matching_size,$time_ms,$time_s" >> "$SUMMARY_FILE"

            echo -e "  ${GREEN}Matching size: $matching_size, Time: $time_ms ms${NC}"
        else
            if grep -q "bad_alloc" "$output_file" 2>/dev/null; then
                echo -e "  ${RED}Out of memory${NC}"
                echo "$graph_name,$cores,OUT_OF_MEMORY,OUT_OF_MEMORY,OUT_OF_MEMORY" >> "$SUMMARY_FILE"
            else
                echo -e "  ${RED}Failed (see $output_file)${NC}"
                echo "$graph_name,$cores,FAILED,FAILED,FAILED" >> "$SUMMARY_FILE"
            fi
        fi
    done

    echo ""
done

echo -e "${BLUE}===================================${NC}"
echo -e "${BLUE}All experiments completed!${NC}"
echo -e "${BLUE}===================================${NC}"
echo -e "Results saved in: ${GREEN}$RESULTS_DIR${NC}"
echo -e "Summary file: ${GREEN}$SUMMARY_FILE${NC}\n"

# Display summary table
echo -e "${YELLOW}Summary of Results:${NC}"
column -t -s',' "$SUMMARY_FILE"
