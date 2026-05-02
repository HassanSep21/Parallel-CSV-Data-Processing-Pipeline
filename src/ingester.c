#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include "common/chunk.h"

int fifo_fd = -1;
volatile sig_atomic_t keep_running = 1;

// Global statistics variables for SIGUSR1 reporting
volatile int files_processed = 0;
volatile int chunks_sent = 0;
volatile size_t total_bytes_sent = 0;

// Handle SIGTERM for graceful shutdown
void handle_sigterm(int sig);

// Handle SIGUSR1 for live statistics
void handle_sigusr1(int sig);

int main(int argc, char *argv[])
{
    char *input_dir = NULL;
    char *fifo_path = NULL;
    int opt;

    while ((opt = getopt(argc, argv, "i:f:")) != -1)
    {
        switch (opt)
        {
        case 'i':
            input_dir = optarg;
            break;
        case 'f':
            fifo_path = optarg;
            break;
        default:
            exit(10);
        }
    }

    if (!input_dir || !fifo_path)
    {
        fprintf(stderr, "[PID: %d, PPID: %d] Ingester: Missing arguments.\n", getpid(), getppid());
        exit(10);
    }

    // Set up signal handlers for BOTH SIGTERM and SIGUSR1
    struct sigaction sa_term;
    struct sigaction sa_usr1;

    sa_term.sa_handler = handle_sigterm;
    sigemptyset(&sa_term.sa_mask);
    sa_term.sa_flags = 0;
    sigaction(SIGTERM, &sa_term, NULL);

    sa_usr1.sa_handler = handle_sigusr1;
    sigemptyset(&sa_usr1.sa_mask);
    sa_usr1.sa_flags = 0; // No SA_RESTART so it can interrupt blocking calls if needed
    sigaction(SIGUSR1, &sa_usr1, NULL);

    printf("[PID: %d, PPID: %d] Ingester: Waiting for processor to open FIFO...\n", getpid(), getppid());

    fifo_fd = open(fifo_path, O_WRONLY);
    if (fifo_fd == -1)
    {
        perror("Ingester: Failed to open FIFO");
        exit(20);
    }
    printf("[PID: %d] Ingester: FIFO opened.\n", getpid());

    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/sample.csv", input_dir);

    FILE *file = fopen(filepath, "r");
    if (!file)
    {
        perror("Ingester: Failed to open CSV file");
        DataChunk eof_chunk;
        memset(&eof_chunk, 0, sizeof(DataChunk));
        eof_chunk.header.is_eof = 1;
        write(fifo_fd, &eof_chunk, sizeof(DataChunk));
        close(fifo_fd);
        exit(40);
    }

    files_processed++; // Track file

    DataChunk chunk;
    memset(&chunk, 0, sizeof(DataChunk));
    int chunk_id = 0;
    size_t bytes_read;

    // Read file in chunks and write to FIFO
    while (keep_running && (bytes_read = fread(chunk.data, 1, MAX_CHUNK_SIZE - 1, file)) > 0)
    {
        chunk.data[bytes_read] = '\0';
        chunk.header.chunk_id = chunk_id++;
        chunk.header.source_file_id = 1;
        chunk.header.byte_count = bytes_read;
        chunk.header.is_eof = 0;

        if (write(fifo_fd, &chunk, sizeof(DataChunk)) == -1)
        {
            perror("Ingester: Failed to write to FIFO");
            break;
        }

        // Update global statistics
        chunks_sent++;
        total_bytes_sent += bytes_read;

        printf("[PID: %d] Ingester: Sent chunk %d (%zu bytes)\n", getpid(), chunk.header.chunk_id, bytes_read);
        memset(&chunk.data, 0, MAX_CHUNK_SIZE);
    }

    fclose(file);

    // Send the EOF chunk
    DataChunk eof_chunk;
    memset(&eof_chunk, 0, sizeof(DataChunk));
    eof_chunk.header.is_eof = 1;
    write(fifo_fd, &eof_chunk, sizeof(DataChunk));
    printf("[PID: %d] Ingester: Sent EOF chunk. Exiting.\n", getpid());

    close(fifo_fd);
    return 0;
}

void handle_sigterm(int sig)
{
    (void)sig;
    keep_running = 0;
}

void handle_sigusr1(int sig)
{
    (void)sig;
    dprintf(STDERR_FILENO, "\n[PID: %d] *** INGESTER LIVE STATS: Files Processed: %d | Chunks Sent: %d | Total Bytes: %zu ***\n",
            getpid(), files_processed, chunks_sent, total_bytes_sent);
}