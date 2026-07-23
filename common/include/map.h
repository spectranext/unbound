#ifndef __MAP_H
#define __MAP_H

#include <stddef.h>
#include <block.h>

typedef uint16_t map_chunk_id;

#define MAP_CHUNK_SIZE (8)
#define MAP_CHUNK_SIZE_SQ (64)
#define MAP_CHUNK_SIZE_DATA_SQ (128)

#define MAX_CLIENT_CACHED_OBJECTS (8)
#define MAX_TEAMS (2)

struct map_chunk_t
{
    block_t data[MAP_CHUNK_SIZE_SQ];
    uint8_t light[MAP_CHUNK_SIZE_SQ];
};

struct map_t
{
    uint8_t width;
    uint8_t height;
};

#define BLOCK_FLAG_ANIMATED 0x20

#define map_chunk_xy(x, y) ((uint16_t)y << 8) | (x)
#define is_block_blocking(x) (x & 0x8000)
#define is_block_fixing(x) (x & 0x4000)
#define strip_block_flags(x) (x & 0xFF)

#endif