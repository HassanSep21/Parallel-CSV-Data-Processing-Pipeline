#ifndef QUEUE_H
#define QUEUE_H

#include "chunk.h"
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>

#define MAX_QUEUE_SIZE 1024

typedef struct
{
    DataChunk buffer[MAX_QUEUE_SIZE];
    int head;
    int tail;
    int count;
    int capacity;

    pthread_mutex_t mutex;
    sem_t *sem_empty;
    sem_t *sem_full;
} ChunkQueue;

void init_queue(ChunkQueue *q, int capacity)
{
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    q->capacity = capacity;

    pthread_mutex_init(&q->mutex, NULL);

    char empty_name[64], full_name[64];
    snprintf(empty_name, sizeof(empty_name), "/sem_empty_%d", getpid());
    snprintf(full_name, sizeof(full_name), "/sem_full_%d", getpid());

    sem_unlink(empty_name);
    sem_unlink(full_name);

    q->sem_empty = sem_open(empty_name, O_CREAT | O_EXCL, 0666, capacity);
    q->sem_full = sem_open(full_name, O_CREAT | O_EXCL, 0666, 0);

    // Unlink immediately
    sem_unlink(empty_name);
    sem_unlink(full_name);
}

void enqueue(ChunkQueue *q, DataChunk *chunk)
{
    sem_wait(q->sem_empty);
    pthread_mutex_lock(&q->mutex);

    q->buffer[q->tail] = *chunk;
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;

    pthread_mutex_unlock(&q->mutex);
    sem_post(q->sem_full);
}

void dequeue(ChunkQueue *q, DataChunk *chunk)
{
    sem_wait(q->sem_full);
    pthread_mutex_lock(&q->mutex);

    *chunk = q->buffer[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->count--;

    pthread_mutex_unlock(&q->mutex);
    sem_post(q->sem_empty);
}

void destroy_queue(ChunkQueue *q)
{
    pthread_mutex_destroy(&q->mutex);
    sem_close(q->sem_empty);
    sem_close(q->sem_full);
}

#endif