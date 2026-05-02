#include <time.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <semaphore.h>
#include "common/shared.h"

volatile sig_atomic_t keep_running = 1;
volatile sig_atomic_t sig_received = 0;

// Signal handler that sets a flag for termination
void handle_signal(int sig);

int main(int argc, char *argv[])
{
    char *input_dir = NULL;
    char *output_dir = NULL;
    char *threads = NULL;
    char *queue_size = NULL;
    char *fifo_path = "/tmp/os_fifo"; // Default FIFO path
    char *shm_name = "/os_shm";       // Default Shared Memory name

    int opt;
    // Parse arguments: -i input, -o output, -n threads, -q queue, -f fifo, -s shm
    while ((opt = getopt(argc, argv, "i:o:n:q:f:s:")) != -1)
    {
        switch (opt)
        {
        case 'i':
            input_dir = optarg;
            break;
        case 'o':
            output_dir = optarg;
            break;
        case 'n':
            threads = optarg;
            break;
        case 'q':
            queue_size = optarg;
            break;
        case 'f':
            fifo_path = optarg;
            break;
        case 's':
            shm_name = optarg;
            break;
        default:
            fprintf(stderr, "Usage: %s -i input -o output -n threads -q queue\n", argv[0]);
            exit(10); // Standard Exit Code 10: Bad command-line arguments
        }
    }

    if (!input_dir || !output_dir || !threads || !queue_size)
    {
        fprintf(stderr, "[PID: %d, PPID: %d] Dispatcher: Missing required arguments.\n", 
                getpid(), getppid());
        exit(10);
    }

    printf("[PID: %d, PPID: %d] Dispatcher: Starting. Threads: %s, Queue: %s\n", 
            getpid(), getppid(), threads, queue_size);

    // Create the FIFO
    // 0666 gives read/write permissions. EEXIST means it already exists, which is fine.
    if (mkfifo(fifo_path, 0666) == -1 && errno != EEXIST)
    {
        perror("Dispatcher: mkfifo failed");
        exit(20);
    }
    printf("[PID: %d] Dispatcher: FIFO created at %s\n", getpid(), fifo_path);

    // Create the POSIX Shared Memory segment
    int shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1)
    {
        perror("Dispatcher: shm_open failed");
        exit(20);
    }

    // Set the size of the shared memory to match our struct
    if (ftruncate(shm_fd, sizeof(SharedData)) == -1)
    {
        perror("Dispatcher: ftruncate failed");
        exit(20);
    }

    // Map the shared memory into the dispatcher's address space
    SharedData *shm_ptr = mmap(NULL, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_ptr == MAP_FAILED)
    {
        perror("Dispatcher: mmap failed");
        exit(20);
    }

    // Initialize the shared memory block to zero
    memset(shm_ptr, 0, sizeof(SharedData));
    printf("[PID: %d] Dispatcher: Shared memory created and initialized.\n", getpid());

    // Spawn the child processes
    pid_t pid_ingester;
    pid_t pid_processor;
    pid_t pid_reporter;

    // --- Spawn Ingester ---
    pid_ingester = fork();
    if (pid_ingester < 0)
    {
        perror("Dispatcher: Fork failed for ingester");
        exit(30);
    }
    else if (pid_ingester == 0)
    {
        // Redirect stdout and stderr to logs/ingester.log
        int log_fd = open("logs/ingester.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (log_fd != -1)
        {
            dup2(log_fd, STDOUT_FILENO);
            dup2(log_fd, STDERR_FILENO);
            close(log_fd);
        }

        char *args[] = {"./ingester", "-i", input_dir, "-f", fifo_path, NULL};
        execvp(args[0], args);
        perror("Dispatcher: execvp failed for ingester");
        exit(30);
    }

    // --- Spawn Processor ---
    pid_processor = fork();
    if (pid_processor < 0)
    {
        perror("Dispatcher: Fork failed for processor");
        exit(30);
    }
    else if (pid_processor == 0)
    {
        // Redirect stdout and stderr to logs/processor.log
        int log_fd = open("logs/processor.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (log_fd != -1)
        {
            dup2(log_fd, STDOUT_FILENO);
            dup2(log_fd, STDERR_FILENO);
            close(log_fd);
        }

        char *args[] = {"./processor", "-n", threads, "-q", queue_size, "-f", fifo_path, "-s", shm_name, NULL};
        execvp(args[0], args);
        perror("Dispatcher: execvp failed for processor");
        exit(30);
    }

    // --- Spawn Reporter ---
    pid_reporter = fork();
    if (pid_reporter < 0)
    {
        perror("Dispatcher: Fork failed for reporter");
        exit(30);
    }
    else if (pid_reporter == 0)
    {
        // Redirect stdout and stderr to logs/reporter.log
        int log_fd = open("logs/reporter.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (log_fd != -1)
        {
            dup2(log_fd, STDOUT_FILENO);
            dup2(log_fd, STDERR_FILENO);
            close(log_fd);
        }

        char *args[] = {"./reporter", "-o", output_dir, "-s", shm_name, NULL};
        execvp(args[0], args);
        perror("Dispatcher: execvp failed for reporter");
        exit(30);
    }

    printf("[PID: %d] Dispatcher: Spawned Ingester(PID: %d), Processor(PID: %d), Reporter(PID: %d)\n", 
            getpid(), pid_ingester, pid_processor, pid_reporter);

    // Install Signal Handlers
    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGCHLD, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);

    // sigsuspend() loop
    sigset_t empty_mask;
    sigemptyset(&empty_mask);

    int children_left = 3;
    time_t start_time = time(NULL);

    printf("[PID: %d] Dispatcher: Entering wait loop...\n", getpid());
    while (children_left > 0 && keep_running)
    {
        // Blocks here without wasting CPU cycles
        sigsuspend(&empty_mask);

        // A signal woke us up. Check if any children died.
        int status;
        pid_t pid;
        // WNOHANG means don't block if no child has actually exited yet
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
        {
            children_left--;
            int exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            time_t runtime = time(NULL) - start_time;
            printf("[PID: %d] Dispatcher: Child %d exited with status %d (Runtime: %ld sec)\n",
                   getpid(), pid, exit_status, runtime);
        }
    }

    // Shutdown Sequence
    if (!keep_running)
    {
        printf("\n[PID: %d] Dispatcher: Shutdown signal (%d) received. Terminating children...\n", 
                getpid(), sig_received);
        // Forward SIGTERM to children so they can exit gracefully
        kill(pid_ingester, SIGTERM);
        kill(pid_processor, SIGTERM);
        kill(pid_reporter, SIGTERM);

        // Wait for them to finish dying
        int status;
        while (waitpid(-1, &status, 0) > 0)
            ;
    }

    // Cleanup IPC Resources (Crucial step!)
    printf("[PID: %d] Dispatcher: Cleaning up resources...\n", getpid());
    unlink(fifo_path);
    munmap(shm_ptr, sizeof(SharedData));
    shm_unlink(shm_name);
    sem_unlink("/os_report_sem");

    // Determine final exit code based on project standard codes
    int final_exit = 0;
    if (!keep_running)
    {
        final_exit = (sig_received == SIGINT) ? 130 : 143;
    }

    printf("[PID: %d] Dispatcher: Pipeline finished. Exiting with code %d.\n", getpid(), final_exit);
    return final_exit;
}

void handle_signal(int sig)
{
    if (sig == SIGINT || sig == SIGTERM)
    {
        keep_running = 0;
        sig_received = sig;
    }
}