#ifndef CHUNK_H
#define CHUNK_H

#define MAX_CHUNK_SIZE 4096

typedef struct
{
    int chunk_id;
    int source_file_id;
    int byte_count;
    int is_eof;
} ChunkHeader;

typedef struct
{
    ChunkHeader header;
    char data[MAX_CHUNK_SIZE];
} DataChunk;

#endif