#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/mman.h>
#include <semaphore.h>
#include "common/shared.h"

int main(int argc, char *argv[])
{
    char *output_dir = NULL;
    char *shm_name = NULL;
    int opt;

    while ((opt = getopt(argc, argv, "o:s:")) != -1)
    {
        switch (opt)
        {
        case 'o':
            output_dir = optarg;
            break;
        case 's':
            shm_name = optarg;
            break;
        default:
            exit(10);
        }
    }

    if (!output_dir || !shm_name)
    {
        fprintf(stderr, "Reporter: Missing arguments.\n");
        exit(10);
    }

    printf("[PID: %d, PPID: %d] Reporter: Waiting for processor to finish...\n", getpid(), getppid());

    // Open the named semaphore
    sem_t *sem_report = sem_open("/os_report_sem", O_CREAT, 0666, 0);
    if (sem_report == SEM_FAILED)
    {
        perror("Reporter: sem_open failed");
        exit(20);
    }

    sem_wait(sem_report);
    printf("[PID: %d] Reporter: Semaphore signaled. Reading shared memory.\n", getpid());

    // Open and map shared memory
    int shm_fd = shm_open(shm_name, O_RDONLY, 0666);
    if (shm_fd == -1)
    {
        perror("Reporter: shm_open failed");
        exit(20);
    }

    SharedData *shm_ptr = mmap(NULL, sizeof(SharedData), PROT_READ, MAP_SHARED, shm_fd, 0);
    if (shm_ptr == MAP_FAILED)
    {
        perror("Reporter: mmap failed");
        exit(20);
    }

    // Write report.csv
    char csv_path[256];
    snprintf(csv_path, sizeof(csv_path), "%s/report.csv", output_dir);
    FILE *csv_file = fopen(csv_path, "w");
    if (csv_file)
    {
        fprintf(csv_file, "DeviceID,TotalSum,ReadingsCount,Average,Anomalies\n");
        for (int i = 0; i < shm_ptr->num_devices; i++)
        {
            DeviceRecord *rec = &shm_ptr->devices[i];
            double avg = (rec->count > 0) ? (rec->total_sum / rec->count) : 0.0;
            fprintf(csv_file, "%s,%.2f,%d,%.2f,%d\n",
                    rec->device_id, rec->total_sum, rec->count, avg, rec->anomaly_count);
        }
        fclose(csv_file);
    }

    // Write report.txt
    char txt_path[256];
    snprintf(txt_path, sizeof(txt_path), "%s/report.txt", output_dir);
    int txt_fd = open(txt_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (txt_fd != -1)
    {
        fflush(stdout);
        
        // Save the current stdout file descriptor
        int saved_stdout = dup(STDOUT_FILENO);

        // Redirect stdout (fd 1) to point to text file
        dup2(txt_fd, STDOUT_FILENO);

        // These printf statements now go to report.txt, not the terminal!
        printf("--- IoT Sensor Aggregation Report ---\n");
        printf("Total Devices Tracked: %d\n\n", shm_ptr->num_devices);
        for (int i = 0; i < shm_ptr->num_devices; i++)
        {
            DeviceRecord *rec = &shm_ptr->devices[i];
            double avg = (rec->count > 0) ? (rec->total_sum / rec->count) : 0.0;
            printf("Device: %s\n", rec->device_id);
            printf("  - Average Reading: %.2f\n", avg);
            printf("  - Total Anomalies: %d\n\n", rec->anomaly_count);
        }
        fflush(stdout);

        // Restore stdout back to the terminal
        dup2(saved_stdout, STDOUT_FILENO);
        close(saved_stdout);
        close(txt_fd);
    }

    // Signal the Dispatcher that the report is ready
    printf("[PID: %d] Reporter: Report generated. Signaling dispatcher.\n", getpid());
    kill(getppid(), SIGUSR1);

    // Cleanup
    munmap(shm_ptr, sizeof(SharedData));
    close(shm_fd);
    sem_close(sem_report);

    return 0;
}