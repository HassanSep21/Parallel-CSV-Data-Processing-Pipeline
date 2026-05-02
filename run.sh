#!/bin/bash

usage() 
{
    echo "Usage: $0 -i <input_dir> -o <output_dir> -n <threads> [-c] [-h]"
    echo "  -i : Input directory containing CSV files"
    echo "  -o : Output directory for reports"
    echo "  -n : Number of threads"
    echo "  -c : Clean build"
    echo "  -h : Show help message"
    exit 0
}

check_dependencies() 
{
    if ! command -v gcc &> /dev/null || ! command -v make &> /dev/null; then
        echo "Error: gcc and make must be installed."
        exit 1
    fi

    # Check if input dir exists and has at least one CSV file
    if [ ! -d "$INPUT_DIR" ]; then
        echo "Error: Input directory '$INPUT_DIR' does not exist."
        exit 40
    fi

    csv_count=$(ls "$INPUT_DIR"/*.csv 2>/dev/null | wc -l)
    if [ "$csv_count" -eq 0 ]; then
        echo "Error: No .csv files found in '$INPUT_DIR'."
        exit 40
    fi
}

cleanup() 
{
    echo -e "\n[Orchestrator] Cleaning up..."
    # If the dispatcher is still running, kill it
    if [ -f ".pid" ]; then
        DISP_PID=$(cat .pid)
        if ps -p "$DISP_PID" > /dev/null; then
            kill -TERM "$DISP_PID" 2>/dev/null
        fi
        rm -f .pid
    fi
}

trap cleanup EXIT INT TERM

INPUT_DIR=""
OUTPUT_DIR=""
THREADS=""
CLEAN=0

# Parse arguments using getopts
while getopts "i:o:n:ch" opt; do
    case ${opt} in
        i ) INPUT_DIR=$OPTARG ;;
        o ) OUTPUT_DIR=$OPTARG ;;
        n ) THREADS=$OPTARG ;;
        c ) CLEAN=1 ;;
        h ) usage ;;
        * ) usage ;;
    esac
done

# Validate arguments
if [ -z "$INPUT_DIR" ] || [ -z "$OUTPUT_DIR" ] || [ -z "$THREADS" ]; then
    echo "Error: Missing required arguments."
    usage
fi

check_dependencies

# Handle clean flag
if [ "$CLEAN" -eq 1 ]; then
    echo "[Orchestrator] Cleaning project..."
    make clean
fi

# Build project
echo "[Orchestrator] Building project..."
if ! make; then
    echo "Error: make failed. Exiting."
    exit 1
fi

mkdir -p "$OUTPUT_DIR"
mkdir -p logs

START_TIME=$(date +%s)

# Launch dispatcher in the background
echo "[Orchestrator] Launching dispatcher..."
./dispatcher -i "$INPUT_DIR" -o "$OUTPUT_DIR" -n "$THREADS" -q 100 -f "/tmp/os_proj_fifo" -s "/os_proj_shm" &
DISP_PID=$!
echo "$DISP_PID" > .pid

# Waits for the dispatcher to finish
wait $DISP_PID
FINAL_STATUS=$?

END_TIME=$(date +%s)
RUNTIME=$((END_TIME - START_TIME))

# Counts records processed
RECORDS=0
if [ -f "$OUTPUT_DIR/report.csv" ]; then
    LINES=$(wc -l < "$OUTPUT_DIR/report.csv")
    if [ "$LINES" -gt 0 ]; then
        RECORDS=$((LINES - 1))
    fi
fi

echo "========================================"
echo "          PIPELINE SUMMARY              "
echo "========================================"
echo "Total Runtime    : $RUNTIME seconds"
echo "Records Processed: $RECORDS devices"
echo "Exit Status      : $FINAL_STATUS"
echo "========================================"

exit $FINAL_STATUS