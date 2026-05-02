#ifndef SHARED_H
#define SHARED_H

#define MAX_DEVICES 100
#define MAX_STRING_LEN 32

// Holds the aggrigated results of a single IoT device
typedef struct
{
    char device_id[MAX_STRING_LEN];
    double total_sum;
    int count;
    int anomaly_count;
} DeviceRecord;

// The main table that lives in shared memory
typedef struct
{
    int num_devices;
    DeviceRecord devices[MAX_DEVICES];
} SharedData;

#endif