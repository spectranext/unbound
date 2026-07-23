#include "server_map.h"
#include "server.h"
#include "utlist.h"
#include "object.h"
#include "proto_objects.h"
#include "utstring.h"
#include <stdlib.h>

void server_map_init(struct server_map_t* map, uint16_t width, uint16_t height)
{
    server_map_free(map);

    map->map.width = width;
    map->map.height = height;
    map->chunks = (struct server_map_chunk_t*)calloc(width * height, sizeof(struct server_map_chunk_t));
    map->objects = NULL;
    map->next_object_id = 1;
}

void server_map_free(struct server_map_t* map)
{
    if (map->chunks == NULL)
        return;

    free(map->chunks);
}

#define CURRENT_MAP_VERSION 0x01

uint8_t server_map_save(struct server_state_t* server_state, struct server_map_t* map, const char* filename)
{
    struct stat st = {0};
    if (stat(filename, &st) == -1) {
#ifdef WIN32
        mkdir(filename);
#else
        mkdir(filename, 0700);
#endif
    }

    {
        UT_string header_filename;
        utstring_init(&header_filename);
        utstring_printf(&header_filename, "%s/header", filename);

        FILE* f = fopen(utstring_body(&header_filename), "wb");
        if (f == NULL) {
            utstring_done(&header_filename);
            return 1;
        }

        utstring_done(&header_filename);

        uint16_t objects_count = HASH_COUNT(map->objects);

        {
            struct server_object_reference_t* object;
            struct server_object_reference_t* tmp;
            HASH_ITER(hh, map->objects, object, tmp)
            {
                if (object->client_id == 0)
                {
                    continue;
                }

                // has to be cached on save, so we report diff numbers
                objects_count--;
            }
        }

        server_printf("saving map, %d objects\n", objects_count);

        // header
        {
            uint8_t ver = CURRENT_MAP_VERSION;

            declare_arg_property_on_stack(version, 'V', ver, NULL);
            declare_arg_property_on_stack(_objects_count, 'O', objects_count, &version);
            declare_arg_property_on_stack(width, 'W', map->map.width, &_objects_count);
            declare_arg_property_on_stack(height, 'H', map->map.height, &width);
            declare_arg_property_on_stack(next_object_id, 'n', map->next_object_id, &height);
            declare_arg_property_on_stack(next_client_id, 'N', server_state->next_client_id, &next_object_id);

            declare_object_on_stack(header, 512, &next_client_id);
            fwrite(proto_object_data_update_size(header), header->object_size + 2, 1, f);
        }

        fclose(f);
    }

    {
        UT_string header_filename;
        utstring_init(&header_filename);
        utstring_printf(&header_filename, "%s/blocks", filename);

        FILE* f = fopen(utstring_body(&header_filename), "wb");
        if (f == NULL) {
            utstring_done(&header_filename);
            return 1;
        }

        utstring_done(&header_filename);

        for (uint16_t iy = 0; iy < map->map.height; iy++)
        {
            for (uint16_t ix = 0; ix < map->map.width; ix++)
            {
                struct server_map_chunk_t* chunk = server_map_get_chunk(&map->map, ix, iy);

                for (uint8_t y = 0; y < MAP_CHUNK_SIZE; y++)
                {
                    for (uint8_t x = 0; x < MAP_CHUNK_SIZE; x++)
                    {
                        ProtoObject* obj = NULL;
                        if (server_python_map_serialize_block(chunk, x, y, &obj))
                        {
                            return 2;
                        }
                        if (obj == NULL)
                        {
                            uint16_t nothing = 0;
                            fwrite(&nothing, 2, 1, f);
                        }
                        else
                        {
                            fwrite(proto_object_data_update_size(obj), obj->object_size + 2, 1, f);
                            free(obj);
                        }
                    }
                }
            }
        }

        fclose(f);
    }

    server_python_map_serialize(&server_state->server_python, filename);

    // dump the objects
    {
        UT_string header_filename;
        utstring_init(&header_filename);
        utstring_printf(&header_filename, "%s/objects", filename);

        FILE* f = fopen(utstring_body(&header_filename), "wb");
        if (f == NULL) {
            utstring_done(&header_filename);
            return 1;
        }

        utstring_done(&header_filename);

        struct server_object_reference_t* object;
        struct server_object_reference_t* tmp;
        HASH_ITER(hh, map->objects, object, tmp)
        {
            if (object->client_id)
            {
                // will be converted
                continue;
            }

            ProtoObject* serialized = NULL;
            if (server_object_serialize(server_state, object, &serialized))
            {
                server_printf("Failed to serialize object %d\n", object->object.object_id);
                fclose(f);
                return 1;
            }

            fwrite(proto_object_data_update_size(serialized), serialized->object_size + 2, 1, f);
            free(serialized);
        }

        fclose(f);
    }

    server_printf("saved map into %s\n", filename);

    return 0;
}

static uint8_t yield_object(FILE* f, uint8_t* buffer, uint16_t buffer_size, ProtoObject** result)
{
    uint16_t object_size;
    if (fread(&object_size, 2, 1, f) == 0)
    {
        return 1;
    }

    if (object_size == 0)
    {
        *result = NULL;
        return 0;
    }

    if (object_size > buffer_size)
    {
        return 2;
    }

    if (fread(buffer, object_size, 1, f) == 0)
    {
        return 3;
    }

    static uintptr_t object_buffer[(512 + sizeof(uintptr_t) - 1) / sizeof(uintptr_t)];

    ProtoObject* obj = (ProtoObject*)object_buffer;
    if (proto_object_read(obj, 128, object_size, buffer))
    {
        return 4;
    }

    *result = obj;
    return 0;
}

static uint8_t yield_object_copy(FILE* f, uint8_t* buffer, uint16_t buffer_size, ProtoObject** result)
{
    ProtoObject* o;
    if (yield_object(f, buffer, buffer_size, &o))
    {
        return 1;
    }

    *result = proto_object_copy(o);
    return 0;
}

uint8_t server_map_load(struct server_state_t* server_state, struct server_map_t* map, const char* filename)
{
    uint16_t objects_count;

    {
        UT_string header_filename;
        utstring_init(&header_filename);
        utstring_printf(&header_filename, "%s/header", filename);

        FILE* f = fopen(utstring_body(&header_filename), "rb");
        if (f == NULL) {
            utstring_done(&header_filename);
            return 1;
        }

        utstring_done(&header_filename);

        uint8_t buffer[2048];

        ProtoObject* header;
        if (yield_object(f, buffer, sizeof(buffer), &header))
        {
            fclose(f);
            return 2;
        }

        uint8_t version = get_uint8_property(header, 'V', 0);

        switch (version)
        {
            case CURRENT_MAP_VERSION:
            {
                break;
            }
            default:
            {
                server_printf("Unsupported version: %d\n", version);
                fclose(f);
                return 3;
            }
        }

        server_map_init(map,
            get_uint8_property(header, 'W', 0),
            get_uint8_property(header, 'H', 0));

        objects_count = get_uint16_property(header, 'O', 0);

        map->next_object_id = get_uint16_property(header, 'n', 0);
        server_state->next_client_id = get_uint16_property(header, 'N', 0);

        fclose(f);
    }

    server_printf("loading map, %d objects\n", objects_count);

    {
        UT_string header_filename;
        utstring_init(&header_filename);
        utstring_printf(&header_filename, "%s/blocks", filename);

        FILE* f = fopen(utstring_body(&header_filename), "rb");
        if (f == NULL) {
            utstring_done(&header_filename);
            return 1;
        }

        utstring_done(&header_filename);

        uint8_t buffer[2048];

        for (uint16_t iy = 0; iy < map->map.height; iy++)
        {
            for (uint16_t ix = 0; ix < map->map.width; ix++)
            {
                for (uint8_t y = 0; y < MAP_CHUNK_SIZE; y++)
                {
                    for (uint8_t x = 0; x < MAP_CHUNK_SIZE; x++)
                    {
                        ProtoObject* obj;
                        if (yield_object(f, buffer, sizeof(buffer), &obj))
                        {
                            server_printf("Not enough data for block %dx%d.%dx%d\n", ix, iy, x, y);
                            fclose(f);
                            return 4;
                        }

                        char identity[64] = {};

                        if (obj == NULL)
                        {
                            identity[0] = '\0';
                        }
                        else
                        {
                            if (get_str_property(obj, OBJ_PROPERTY_ID, identity, sizeof(identity)))
                            {
                                server_printf("No identity for block %dx%d.%dx%d\n", ix, iy, x, y);
                                fclose(f);
                                return 5;
                            }
                        }

                        PyObject* py = server_python_allocate_block(
                            &server_state->server_python, identity, obj);

                        if (py == NULL)
                        {
                            server_printf("Cannot allocate block for identity %s at %dx%d.%dx%d\n",
                                identity, ix, iy, x, y);
                            fclose(f);
                            return 6;
                        }

                        if (py == Py_None)
                        {
                            server_printf("Identity %s yielded to None at %dx%d.%dx%d\n",
                                identity, ix, iy, x, y);
                        }
                        else
                        {
                            server_python_map_set_py(server_state, map, ix * MAP_CHUNK_SIZE + x,
                                iy * MAP_CHUNK_SIZE + y, py, 0);
                        }
                    }
                }
            }
        }


        fclose(f);
    }

    server_python_map_deserialize(&server_state->server_python, filename);

    {
        UT_string header_filename;
        utstring_init(&header_filename);
        utstring_printf(&header_filename, "%s/objects", filename);

        FILE* f = fopen(utstring_body(&header_filename), "rb");
        if (f == NULL) {
            utstring_done(&header_filename);
            return 1;
        }

        utstring_done(&header_filename);

        uint8_t buffer[2048];

        for (uint16_t i = 0; i < objects_count; i++)
        {
            ProtoObject* obj;
            if (yield_object(f, buffer, sizeof(buffer), &obj))
            {
                server_printf("Not enough objects for object %d\n",i);
                fclose(f);
                return 7;
            }

            char identity[32];

            if (get_str_property(obj, '_', identity, sizeof(identity)))
            {
                server_printf("Can't get identity for object %d\n", i);
                continue;
            }

            PyObject* py = server_python_allocate_py_object(&server_state->server_python, identity);
            if (py == NULL)
            {
                server_printf("Can't allocate object for object %d of kind %s\n", i, identity);
                continue;
            }

            uint16_t object_id = get_uint16_property(obj, 'i', 0xFFFF);
            uint16_t x = get_uint16_property(obj, 'x', 0);
            uint16_t y = get_uint16_property(obj, 'y', 0);

            struct server_object_reference_t* ref = map_add_object(server_state, &map->map, x, y, object_id);
            map_object_assign_py(server_state, ref, py);

            if (server_object_deserialize(server_state, obj, ref))
            {
                server_printf("Can't deserialize for object %d of kind %s\n", i, identity);
                fclose(f);
                return 9;
            }

            map_object_finalize(server_state, ref);
            ref->object.payload = 0;
        }

        fclose(f);
    }

    server_printf("Loaded map from %s\n", filename);
    return 0;
}

struct server_map_chunk_t* server_map_get_chunk(struct map_t* map, uint16_t ix, uint16_t iy)
{
    if (ix >= map->width || iy >= map->height)
    {
        return NULL;
    }

    struct server_map_t* server_map = (struct server_map_t*)map;
    return &server_map->chunks[ix + iy * map->width];
}

struct block_metadata_t* server_map_get_block_metadata(struct map_t* map, uint16_t x, uint16_t y)
{
    uint8_t chunk_x = x / MAP_CHUNK_SIZE;
    uint8_t chunk_y = y / MAP_CHUNK_SIZE;
    uint8_t local_x = x % MAP_CHUNK_SIZE;
    uint8_t local_y = y % MAP_CHUNK_SIZE;

    struct server_map_chunk_t* chunk = server_map_get_chunk(map, chunk_x, chunk_y);
    if (chunk == NULL)
    {
        return NULL;
    }

    return &chunk->block_metadata[local_x + local_y * MAP_CHUNK_SIZE];
}

struct map_chunk_t* map_get_chunk(struct map_t* map, uint16_t ix, uint16_t iy)
{
    return &server_map_get_chunk(map, ix, iy)->chunk;
}

struct map_chunk_t* map_get_chunk_at(struct map_t* map, uint16_t x, uint16_t y)
{
    return &server_map_get_chunk(map, x / MAP_CHUNK_SIZE, y / MAP_CHUNK_SIZE)->chunk;
}

struct server_object_reference_t* server_map_get_object(struct server_map_t* map, uint16_t object_id)
{
    struct server_object_reference_t* result = NULL;
    HASH_FIND(hh, map->objects, &object_id, sizeof(uint16_t), result);
    return result;
}

struct server_object_reference_t* map_add_object(struct server_state_t* server_state, struct map_t* map, uint16_t xx, uint16_t yy, uint16_t oid)
{
    uint8_t chunk_x = OBJECT_PHY_TO_LOGICAL_CHUNK(xx);
    uint8_t chunk_y = OBJECT_PHY_TO_LOGICAL_CHUNK(yy);

    struct server_object_reference_t* ref = calloc(1, sizeof(struct server_object_reference_t));
    ref->object.object_id = oid == 0xFFFF ? ((struct server_map_t*)map)->next_object_id++ : oid;
    ref->object.location.x = xx;
    ref->object.location.y = yy;
    ref->object.target.x = ref->object.location.x;
    ref->object.target.y = ref->object.location.y;
    ref->object.speed.x = 0;
    ref->object.speed.y = 0;
    ref->py_object = NULL;

    HASH_ADD(hh, ((struct server_map_t*)map)->objects, object.object_id, sizeof(uint16_t), ref);
    init_object(&ref->object);

    server_printf("added object %d at %dx%d\n", ref->object.object_id, chunk_x, chunk_y);
    server_state_schedule_map_refresh(server_state, 200);
    return ref;
}

void map_object_assign_py(struct server_state_t* server_state, struct server_object_reference_t* ref, PyObject* obj)
{
    if (ref->py_object)
    {
        Py_DecRef(ref->py_object);
    }

    ref->py_object = obj;

    server_python_assign_py_callbacks(&server_state->server_python, obj, ref);

    Py_IncRef(ref->py_object);
}

static uint8_t get_object_data_entry(struct server_state_t* server_state, struct server_object_reference_t* ref,
    const char* kind)
{
    const char* data_entry = server_python_object_py_get_data_entry(&server_state->server_python, ref, kind);

    if (data_entry == NULL)
    {
        return 0;
    }

    struct server_data_entry_t* e = find_data_entry(&server_state->server_data, data_entry);
    if (e)
    {
        if (get_data_entry_prop_int(e, "PRESHIFTED", 0))
        {
            return e->index | 0x80;
        }
        else
        {
            return e->index;
        }
    }
    else
    {
        return 0;
    }
}

void map_object_finalize(struct server_state_t* server_state, struct server_object_reference_t* ref)
{
    struct map_object_t* o = &ref->object;

    o->data_id = get_object_data_entry(server_state, ref, "data_entry");
    o->data_move_id = get_object_data_entry(server_state, ref, "move_entry");
    o->data_picking_id = get_object_data_entry(server_state, ref, "picking_entry");
    o->sprite_data_id = o->data_id;

    o->team_id = server_python_get_team_id_object_py(&server_state->server_python, ref);
    o->type = server_python_object_py_get_type(&server_state->server_python, ref);
    server_python_init_object_py(&server_state->server_python, ref);
    ref->prediction_animation = 0;
    ref->prediction_animation_time_ms = server_time() - SERVER_OBJECT_SPRITE_ANIMATION_FRAME_MS;
    ref->predictions_stationary = 0;
    generate_object_predictions(ref);
}

uint8_t map_delete_object(struct server_state_t* server_state, struct map_t* map, uint16_t object_id)
{
    struct server_object_reference_t* ref = NULL;
    HASH_FIND(hh, ((struct server_map_t*)map)->objects, &object_id, sizeof(uint16_t), ref);

    if (ref == NULL)
        return 0;

    server_python_object_free_object_py(&server_state->server_python, ref);
    server_object_clear_overlaps(ref);

    {
        struct client_state_t* client_state;
        LL_FOREACH(server_state->client_states, client_state)
        {
            server_state_client_unsync_object(client_state, server_state, &ref->object);
        }
    }

    server_printf("deleted object id %d at %dx%d\n", object_id,
        OBJECT_PHY_TO_LOGICAL(ref->object.location.x),
        OBJECT_PHY_TO_LOGICAL(ref->object.location.y));

    HASH_DEL(((struct server_map_t*)map)->objects, ref);
    free(ref);
    return 1;
}

uint8_t map_mark_object_to_delete(struct server_state_t* server_state, struct map_t* map, uint16_t object_id)
{
    struct server_object_reference_t* ref = NULL;
    HASH_FIND(hh, ((struct server_map_t*)map)->objects, &object_id, sizeof(uint16_t), ref);

    if (ref == NULL)
        return 0;

    if (ref->destroyed)
        return 1;

    server_python_object_free_object_py(&server_state->server_python, ref);
    server_object_clear_overlaps(ref);

    struct server_object_delete_queue_t* queued = calloc(1, sizeof(struct server_object_delete_queue_t));
    queued->object_id = object_id;
    LL_APPEND(server_state->objects_to_delete, queued);
    return 1;
}

static void map_sync_object_state(struct server_state_t* server_state, struct map_t* map,
    struct server_object_reference_t* o)
{
    uint8_t chunk_x = OBJECT_PHY_TO_LOGICAL(o->object.location.x) / MAP_CHUNK_SIZE;
    uint8_t chunk_y = OBJECT_PHY_TO_LOGICAL(o->object.location.y) / MAP_CHUNK_SIZE;

    {
        struct client_state_t* client_state;
        LL_FOREACH(server_state->client_states, client_state)
        {
            if (server_map_chunk_get_subscriber(&server_state->map, chunk_x, chunk_y, client_state))
            {
                server_state_client_sync_state(client_state, server_state, &o->object);
            }
        }
    }

    server_printf("obj %d state %d flags %d\n", o->object.object_id, o->object.state, o->object.state_flags);
}

void map_send_effect(struct server_state_t* server_state, const char* effect, uint16_t x, uint16_t y)
{
    struct client_state_t* client_state;
    LL_FOREACH(server_state->client_states, client_state)
    {
        server_state_client_send_effect(client_state, server_state, effect, x, y);
    }
}

void map_set_object_state(struct server_state_t* server_state, struct map_t* map,
    struct server_object_reference_t* o, enum client_object_state_t state)
{
    o->object.state = state;
    generate_object_predictions(o);
    map_sync_object_state(server_state, map, o);
}

void map_set_object_state_flags(struct server_state_t* server_state, struct map_t* map,
    struct server_object_reference_t* o, uint8_t state_flags)
{
    o->object.state_flags = state_flags;
    generate_object_predictions(o);
    map_sync_object_state(server_state, map, o);
}

enum client_object_state_t map_get_object_state(struct server_object_reference_t* o)
{
    return o->object.state;
}

struct server_map_chunk_subscriber_t* server_map_chunk_get_subscriber(struct server_map_t* map, uint16_t ix, uint16_t iy, struct client_state_t* client_state)
{
    struct server_map_chunk_t* chunk = server_map_get_chunk(&map->map, ix, iy);
    if (chunk == NULL)
        return NULL;

    struct server_map_chunk_subscriber_t* sub = NULL;

    LL_SEARCH_SCALAR(chunk->subscribers, sub, client_state, client_state);

    return sub;
}

uint8_t server_map_get_next_non_synced_chunk(struct client_state_t* client_state)
{
    uint8_t id = 0;
    // look for fresh first
    while (id < CLIENT_MAX_SYNCED_CHUNKS)
    {
        if (client_state->synced_chunks[id] == 0)
            return id;
        id++;
    }

    // look for dirty
    while (id < CLIENT_MAX_SYNCED_CHUNKS)
    {
        if (client_state->synced_chunks[id] == 0xFF)
            return id;
        id++;
    }

    return 0xFF;
}

struct server_map_chunk_subscriber_t* server_map_chunk_subscribe(struct server_map_t* map, uint16_t ix, uint16_t iy, struct client_state_t* client_state)
{
    struct server_map_chunk_t* chunk = server_map_get_chunk(&map->map, ix, iy);

    uint8_t id = server_map_get_next_non_synced_chunk(client_state);
    if (id == 0xFF)
        return NULL;

    struct server_map_chunk_subscriber_t* sub = calloc(1, sizeof(struct server_map_chunk_subscriber_t));
    sub->dirty = 0;
    sub->id = id;
    sub->client_state = client_state;

    LL_APPEND(chunk->subscribers, sub);
    client_state->synced_chunks[sub->id] = 1;
    return sub;
}

uint8_t server_map_chunk_unsubscribe(struct server_map_t* map, uint16_t ix, uint16_t iy, struct client_state_t* client_state)
{
    struct server_map_chunk_t* chunk = server_map_get_chunk(&map->map, ix, iy);
    struct server_map_chunk_subscriber_t* sub = NULL;

    LL_SEARCH_SCALAR(chunk->subscribers, sub, client_state, client_state);

    if (sub)
    {
        LL_DELETE(chunk->subscribers, sub);
        uint8_t result = sub->id;
        free(sub);
        client_state->synced_chunks[result] = 0xFF;
        return result;
    }

    return 0xFF;
}

void map_set_block(struct map_t* map, uint16_t x, uint16_t y, block_t block, uint8_t light)
{
    struct map_chunk_t* chunk = map_get_chunk_at(map, x, y);
    if (chunk == NULL)
    {
        return;
    }

    uint16_t idx = x % MAP_CHUNK_SIZE + (y % MAP_CHUNK_SIZE) * MAP_CHUNK_SIZE;
    chunk->data[idx] = block;
    chunk->light[idx] = light;
}

block_t map_get_block(struct map_t* map, uint16_t x, uint16_t y)
{
    struct map_chunk_t* chunk = map_get_chunk_at(map, x, y);
    if (chunk == NULL)
    {
        return 0;
    }

    return chunk->data[x % MAP_CHUNK_SIZE + (y % MAP_CHUNK_SIZE) * MAP_CHUNK_SIZE];
}

uint8_t map_get_light(struct map_t* map, uint16_t x, uint16_t y)
{
    struct map_chunk_t* chunk = map_get_chunk_at(map, x, y);
    if (chunk == NULL)
    {
        return 0;
    }

    return chunk->light[x % MAP_CHUNK_SIZE + (y % MAP_CHUNK_SIZE) * MAP_CHUNK_SIZE];
}

void map_chunk_fill(struct map_chunk_t* chunk, block_t block)
{
    memset(chunk->data, block, MAP_CHUNK_SIZE_DATA_SQ);
}
