# OS Term Project: Parallel CSV Data Processing Pipeline

This project implements a multi-process, multi-threaded data processing pipeline in C. It is designed to process **IoT sensor logs** using POSIX-compliant system calls and inter-process communication (IPC).

## Prerequisites

- macOS or Linux.
- `gcc` compiler.
- `make`.

## Directory Structure

```
project_root/
├── Makefile
├── run.sh
├── README.md
├── src/
│   ├── dispatcher.c
│   ├── ingester.c
│   ├── processor.c
│   ├── reporter.c
│   └── common/
│       ├── chunk.h
│       ├── queue.h
│       └── shared.h
└── data/
    └── sample.csv
```

## Compilation and Execution

The project is orchestrated via a bash script (`run.sh`).

1. Make the orchestrator script executable:
   ```bash
   chmod +x run.sh
   ```

2. Run the pipeline:
   ```bash
   ./run.sh -c -i data -o output -n 4
   ```

### Arguments:
- `-c` : Clean build.
- `-i` : Input directory containing the CSV file.
- `-o` : Output directory for the generated reports.
- `-n` : Number of threads.

## Expected Output

The orchestrator will output a pipeline summary to the terminal on successful execution. 

The pipeline outputs to two locations:
1. **`output/`**: Contains the final collective results.
   - `report.csv`
   - `report.txt`
2. **`logs/`**: Contains the `stdout` and `stderr` for each child process.
   - `ingester.log`
   - `processor.log`
   - `reporter.log`