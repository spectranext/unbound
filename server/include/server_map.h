#ifndef __SERVER_MAP_H
#define __SERVER_MAP_H

#include <stdint.h>
#include "map.h"
#include "server_object.h"

struct client_state_t;
struct server_state_t;

struct server_map_chunk_subscriber_outgoing_block_t
{
    union
    {
        uint16_t xy;
        struct
        {
            uint8_t x;
            uint8_t y;
        };
    };
    block_t code;
    UT_hash_handle hh;
};

struct server_map_chunk_subscriber_t
{
    struct client_state_t* client_state;
    uint8_t dirty;
    uint8_t id;
    uint8_t delay;
    struct server_map_chunk_subscriber_outgoing_block_t* modified_blocks;
    uint8_t modified_blocks_count;
    struct server_map_chunk_subscriber_t* next;
};

#ifndef PyObject_HEAD
struct _object;
typedef struct _object PyObject;
#endif

struct block_metadata_t
{
    PyObject* py_object;
};

struct server_map_chunk_t
{
    struct map_chunk_t chunk;
    struct block_metadata_t block_metadata[MAP_CHUNK_SIZE_SQ];
    struct server_map_chunk_subscriber_t* subscribers;
};

struct server_map_t
{
    struct map_t map;
    struct server_map_chunk_t* chunks;
    struct server_object_reference_t* objects;
    uint16_t next_object_id;
};

extern struct server_object_reference_t* map_add_object(struct server_state_t* server_state, struct map_t* map, uint16_t xx, uint16_t yy, uint16_t oid);
extern void map_object_assign_py(struct server_state_t* server_state, struct server_object_reference_t* ref, PyObject* obj);
extern void map_object_finalize(struct server_state_t* server_state, struct server_object_reference_t* ref);
extern uint8_t map_mark_object_to_delete(struct server_state_t* server_state, struct map_t* map, uint16_t object_id);
extern uint8_t map_delete_object(struct server_state_t* server_state, struct map_t* map, uint16_t object_id);
extern void map_set_object_state(struct server_state_t* server_state, struct map_t* map,
    struct server_object_reference_t* o, enum client_object_state_t state);
extern void map_set_object_state_flags(struct server_state_t* server_state, struct map_t* map,
    struct server_object_reference_t* o, uint8_t state_flags);
extern enum client_object_state_t map_get_object_state(struct server_object_reference_t* o);

extern void map_send_effect(struct server_state_t* server_state, const char* effect, uint16_t x, uint16_t y);

extern void map_set_block(struct map_t* map, uint16_t x, uint16_t y, block_t block, uint8_t light);
extern block_t map_get_block(struct map_t* map, uint16_t x, uint16_t y);
extern uint8_t map_get_light(struct map_t* map, uint16_t x, uint16_t y);
extern void map_chunk_fill(struct map_chunk_t* chunk, block_t block);

extern struct map_chunk_t* map_get_chunk(struct map_t* map, uint16_t ix, uint16_t iy);
extern struct map_chunk_t* map_get_chunk_at(struct map_t* map, uint16_t x, uint16_t y);

extern void server_map_init(struct server_map_t* map, uint16_t width, uint16_t height);
extern uint8_t server_map_save(struct server_state_t* server_state, struct server_map_t* map, const char* filename);
extern uint8_t server_map_load(struct server_state_t* server_state, struct server_map_t* map, const char* filename);
extern void server_map_free(struct server_map_t* map);

extern struct server_object_reference_t* server_map_get_object(struct server_map_t* map, uint16_t object_id);
extern struct server_map_chunk_t* server_map_get_chunk(struct map_t* map, uint16_t ix, uint16_t iy);
extern struct block_metadata_t* server_map_get_block_metadata(struct map_t* map, uint16_t x, uint16_t y);
extern struct server_map_chunk_subscriber_t* server_map_chunk_get_subscriber(struct server_map_t* map, uint16_t ix, uint16_t iy, struct client_state_t* client_state);
extern struct server_map_chunk_subscriber_t* server_map_chunk_subscribe(struct server_map_t* map, uint16_t ix, uint16_t iy, struct client_state_t* client_state);
extern uint8_t server_map_chunk_unsubscribe(struct server_map_t* map, uint16_t ix, uint16_t iy, struct client_state_t* client_state);

#endif
