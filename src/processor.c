#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>
#include "common/queue.h"
#include "common/shared.h"

ChunkQueue queue;
SharedData *shm_ptr;
int fifo_fd;

pthread_mutex_t aggregation_mutex = PTHREAD_MUTEX_INITIALIZER;
int global_num_threads;

// Finds or creates a device record in shared memory
DeviceRecord *get_device_record(char *device_id);

void *reader_thread(void *arg);
void *worker_thread(void *arg);

int main(int argc, char *argv[])
{
    int num_threads = 0;
    int queue_size = 0;
    char *fifo_path = NULL;
    char *shm_name = NULL;
    
    int opt;
    while ((opt = getopt(argc, argv, "n:q:f:s:")) != -1)
    {
        switch (opt)
        {
        case 'n':
            num_threads = atoi(optarg);
            break;
        case 'q':
            queue_size = atoi(optarg);
            break;
        case 'f':
            fifo_path = optarg;
            break;
        case 's':
            shm_name = optarg;
            break;
        default:
            exit(10);
        }
    }

    if (!num_threads || !queue_size || !fifo_path || !shm_name)
    {
        fprintf(stderr, "Processor: Missing arguments.\n");
        exit(10);
    }

    global_num_threads = num_threads;

    // Map the shared memory created by the dispatcher
    int shm_fd = shm_open(shm_name, O_RDWR, 0666);
    if (shm_fd == -1)
    {
        perror("Processor: shm_open failed");
        exit(20);
    }

    shm_ptr = mmap(NULL, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_ptr == MAP_FAILED)
    {
        perror("Processor: mmap failed");
        exit(20);
    }

    // Initialize our bounded buffer queue
    init_queue(&queue, queue_size);

    // Open FIFO for reading
    printf("[PID: %d, PPID: %d] Processor: Opening FIFO...\n", getpid(), getppid());
    fifo_fd = open(fifo_path, O_RDONLY);
    if (fifo_fd == -1)
    {
        perror("Processor: FIFO open failed");
        exit(20);
    }

    // Set up Thread Attributes
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    pthread_attr_setstacksize(&attr, 2 * 1024 * 1024);

    // Create threads
    pthread_t reader;
    pthread_create(&reader, &attr, reader_thread, NULL);

    pthread_t *workers = malloc(sizeof(pthread_t) * num_threads);
    int *worker_ids = malloc(sizeof(int) * num_threads);

    for (int i = 0; i < num_threads; i++)
    {
        worker_ids[i] = i + 1;
        pthread_create(&workers[i], &attr, worker_thread, &worker_ids[i]);
    }

    // Wait for all threads to finish
    pthread_join(reader, NULL);
    for (int i = 0; i < num_threads; i++)
    {
        pthread_join(workers[i], NULL);
    }
    printf("[PID: %d] Processor: All threads finished processing.\n", getpid());

    // Signal the Reporter using a named semaphore
    sem_t *sem_report = sem_open("/os_report_sem", O_CREAT, 0666, 0);
    if (sem_report == SEM_FAILED)
    {
        perror("Processor: sem_open failed");
        exit(20);
    }

    sem_post(sem_report);
    sem_close(sem_report);
    printf("[PID: %d] Processor: Signaled reporter. Exiting.\n", getpid());

    // Cleanup
    pthread_attr_destroy(&attr);
    free(workers);
    free(worker_ids);
    destroy_queue(&queue);
    close(fifo_fd);

    return 0;
}

DeviceRecord *get_device_record(char *device_id)
{
    for (int i = 0; i < shm_ptr->num_devices; i++)
    {
        if (strcmp(shm_ptr->devices[i].device_id, device_id) == 0)
        {
            return &shm_ptr->devices[i];
        }
    }

    if (shm_ptr->num_devices < MAX_DEVICES)
    {
        DeviceRecord *new_device = &shm_ptr->devices[shm_ptr->num_devices];
        strcpy(new_device->device_id, device_id);
        new_device->total_sum = 0;
        new_device->count = 0;
        new_device->anomaly_count = 0;
        shm_ptr->num_devices++;
        return new_device;
    }
    return NULL;
}

void *reader_thread(void *arg)
{
    (void)arg;
    printf("[PID: %d] Processor: Reader thread started.\n", getpid());
    DataChunk chunk;

    while (1)
    {
        ssize_t bytes = read(fifo_fd, &chunk, sizeof(DataChunk));
        if (bytes <= 0)
        {
            break;
        }

        if (chunk.header.is_eof)
        {
            printf("[PID: %d] Processor: EOF received. Shutting down workers.\n", getpid());
            for (int i = 0; i < global_num_threads; i++)
            {
                enqueue(&queue, &chunk);
            }
            break;
        }
        else
        {
            enqueue(&queue, &chunk);
        }
    }
    return NULL;
}

void *worker_thread(void *arg)
{
    int worker_id = *(int *)arg;
    printf("[PID: %d] Processor: Worker %d started.\n", getpid(), worker_id);
    DataChunk chunk;

    while (1)
    {
        dequeue(&queue, &chunk);

        if (chunk.header.is_eof)
        {
            printf("[PID: %d] Processor: Worker %d received EOF. Exiting.\n", getpid(), worker_id);
            break;
        }

        char *saveptr_line;
        char *line = strtok_r(chunk.data, "\n", &saveptr_line);

        while (line != NULL)
        {
            char *saveptr_comma;
            char *device_id = strtok_r(line, ",", &saveptr_comma);
            if (device_id)
            {
                char *val_str = strtok_r(NULL, ",", &saveptr_comma);
                while (val_str != NULL)
                {
                    double val = atof(val_str);

                    pthread_mutex_lock(&aggregation_mutex);

                    DeviceRecord *rec = get_device_record(device_id);
                    if (rec)
                    {
                        rec->total_sum += val;
                        rec->count++;

                        // Any reading > 100 is an anomaly
                        if (val > 100.0)
                        {
                            rec->anomaly_count++;
                        }
                    }

                    pthread_mutex_unlock(&aggregation_mutex);

                    val_str = strtok_r(NULL, ",", &saveptr_comma);
                }
            }
            line = strtok_r(NULL, "\n", &saveptr_line);
        }
    }
    return NULL;
}