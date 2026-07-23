#include "server_data.h"
#include "utils.h"
#include "server.h"
#include "messages.h"

#include <ut/utlist.h>
#include <ut/uthash.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>

struct server_data_kv_t* get_data_entry_prop(struct server_data_entry_t* e, const char* key)
{
    struct server_data_kv_t* kv = NULL;
    HASH_FIND_STR(e->extra, key, kv);
    return kv;
}

uint8_t get_data_entry_prop_int(struct server_data_entry_t* e, const char* key, uint8_t def)
{
    struct server_data_kv_t* p = get_data_entry_prop(e, key);
    if (p == NULL)
        return def;
    if (p->type != SERVER_KV_INT)
        return def;
    return p->as_int;
}

const char* get_data_entry_prop_str(struct server_data_entry_t* e, const char* key)
{
    struct server_data_kv_t* p = get_data_entry_prop(e, key);
    if (p == NULL)
        return NULL;
    if (p->type != SERVER_KV_STRING)
        return NULL;
    return p->as_string;
}

struct server_data_entry_t* find_data_entry(struct server_data_t* server_data, const char* name)
{
    if (name == NULL)
    {
        return NULL;
    }
    struct server_data_entry_t* e = NULL;
    HASH_FIND_STR(server_data->keys, name, e);
    return e;
}

void server_screens_init(struct server_state_t* server_state)
{
    server_state->screens = NULL;

    struct dirent *dir;
    DIR *d = opendir("./pages/screens");
    if (!d)
        return;

    while ((dir = readdir(d)) != NULL)
    {
        if (dir->d_name[0] == '.')
            continue;

        const char *dot = strrchr(dir->d_name, '.');
        if (dot == NULL)
            continue;

        if (strcmp(dot, ".scr") != 0)
            continue;

        char name[64];
        sprintf(name, "%.*s", (int) (dot - dir->d_name), dir->d_name);

        char fname[128];
        sprintf(fname, "pages/screens/%s", dir->d_name);
        FILE* f = fopen(fname, "rb");
        if (f == NULL)
            continue;

        struct server_screen_t* screen = calloc(1, sizeof(struct server_screen_t));
        screen->name = strdup(name);

        server_printf("read screen %s\n", screen->name);
        fread(screen->data, sizeof(screen->data), 1, f);
        fclose(f);

        HASH_ADD_STR(server_state->screens, name, screen);
    }
    closedir(d);
}

uint8_t server_data_init(struct server_data_t* server_data, const char* filename)
{
    server_data->entries_count = 0;
    server_data->data_entries = NULL;
    server_data->keys = NULL;

    FILE* f = fopen(filename, "r");
    if (f == NULL)
    {
        return 1;
    }

    static char line_buffer[40000];

    struct server_data_entry_t* last_entry = NULL;

    while (fgets(line_buffer, sizeof(line_buffer), f))
    {
        if (strlen(line_buffer) == 0)
            continue;

        if (*line_buffer == '\n')
            continue;

        if ((strlen(line_buffer) >= 4) && (memcmp("    ", line_buffer, 4) == 0))
        {
            char* p = line_buffer + 4;

            if (strlen(p) == 0)
                continue;

            if (*p == '\n')
                continue;

            char* equals = strchr(line_buffer, '=');
            if (equals == NULL)
                continue;

            *equals = '\0';

            if (strlen(p) > 31)
            {
                printf("Skipped kv: %s exceeds 31 bytes\n", p);
            }

            struct server_data_kv_t* kv = calloc(1, sizeof(struct server_data_kv_t));
            strcpy(kv->name, p);
            equals++;
            if (*equals == '"')
            {
                kv->type = SERVER_KV_STRING;
                equals++;
                kv->as_string = strndup(equals, strlen(equals) - 2);
            }
            else
            {
                kv->type = SERVER_KV_INT;
                kv->as_int = atoi(equals);
            }

            HASH_ADD_STR(last_entry->extra, name, kv);
            continue;
        }

        char* equals = strchr(line_buffer, '=');
        if (equals == NULL)
            continue;

        *equals = '\0';

        if (strlen(line_buffer) > 31)
        {
            printf("Skipped data: %s exceeds 31 bytes\n", line_buffer);
        }

        struct server_data_entry_t* new_entry = calloc(1, sizeof(struct server_data_entry_t));
        strcpy(new_entry->name, line_buffer);


        equals++;

        if (equals[strlen(equals)-1] == '\n')
            equals[strlen(equals)-1] = '\0';

        new_entry->payload = hex_to_bytes(equals, &new_entry->payload_len);

        last_entry = new_entry;

        server_printf("MODULE: %s (%d bytes)\n", new_entry->name, new_entry->payload_len);

        DL_APPEND(server_data->data_entries, new_entry);
        HASH_ADD_STR(server_data->keys, name, new_entry);
    }

    fclose(f);
    return 0;
}

uint8_t server_data_post_process(struct server_data_t* server_data)
{
    struct server_data_entry_t* tiles = find_data_entry(server_data, "TILES");
    struct server_data_entry_t* tiles_c = find_data_entry(server_data, "TILES_C");

    if (tiles == NULL || tiles_c == NULL)
        return 1;

    /*
     * [first row of tile 0] [first row of tile 1] ... [first row of tile 255]
     * [second row of tile 0] [second row of tile 1] ... [second row of tile 255]
     * ... etc
     */

    uint8_t* combined_tiles = calloc(1, 2304); // 256 * (8 + 1)

    uint16_t tile_count = tiles_c->payload_len;
    for (uint16_t i = 0; i < tile_count; i++)
    {
        uint8_t* tile_data = &tiles->payload[i * 8];
        uint8_t* attr_data = &tiles_c->payload[i];

        for (uint16_t bit = 0; bit < 8; bit++)
        {
            combined_tiles[i + 256 * bit] = tile_data[bit];
        }

        combined_tiles[i + 2048] = *attr_data;
    }

    struct server_data_entry_t* new_entry = calloc(1, sizeof(struct server_data_entry_t));
    strcpy(new_entry->name, "TILES");
    new_entry->payload = combined_tiles;
    new_entry->payload_len = 2304;

    // dispose original sets
    DL_DELETE(server_data->data_entries, tiles);
    DL_DELETE(server_data->data_entries, tiles_c);
    HASH_DEL(server_data->keys, tiles);
    HASH_DEL(server_data->keys, tiles_c);
    free(tiles);
    free(tiles_c);

    // add new "TILES" section
    DL_PREPEND(server_data->data_entries, new_entry);
    HASH_ADD_STR(server_data->keys, name, new_entry);

    // index everything
    struct server_data_entry_t* el;
    DL_FOREACH(server_data->data_entries, el)
    {
        el->index = server_data->entries_count++;
        server_printf("DATA %d: %s %d bytes\n", el->index, el->name, el->payload_len);
    }

    return 0;
}

static void server_data_push_module_namespace(struct client_state_t* client_state,
    struct server_data_entry_t* data_entry, size_t data_offset, size_t payload_len, const char* namespace_name)
{
    struct server_state_t* server_state = client_state->state;

    struct server_data_kv_t* namespace_p = get_data_entry_prop(data_entry, namespace_name);
    if (namespace_p == NULL)
        return;

    if (namespace_p->type != SERVER_KV_INT)
        return;

    uint8_t namespace = namespace_p->as_int;

    uint16_t max_chunk_size = 1600;

    client_printf(client_state, "pushing namespace %s (%d)\n", namespace_name, namespace);

    declare_arg_property_on_stack(_namespace, 'n', namespace, NULL);
    uint8_t command = MSG_MODULE;
    declare_arg_property_on_stack(id, OBJ_PROPERTY_ID, command, &_namespace);

    if (payload_len > max_chunk_size)
    {
        size_t msg_count = payload_len / max_chunk_size;
        ProtoObject** objects = calloc(msg_count + 1, sizeof(ProtoObject*));

        uint8_t* ptr = data_entry->payload + data_offset;
        uint16_t limit = max_chunk_size;
        uint16_t offset = 0;
        uint16_t remaining = payload_len - limit;

        {
            declare_variable_property_on_stack(data, 'p', ptr, limit, &id);

            objects[0] = proto_object_allocate(&data);
        }

        uint8_t msg_id = 1;

        ptr += limit;
        offset += limit;

        while (remaining > 0)
        {
            limit = remaining > max_chunk_size ? max_chunk_size : remaining;

            declare_arg_property_on_stack(_offset, 'o', offset, &id);
            declare_variable_property_on_stack(data, 'p', ptr, limit, &_offset);
            objects[msg_id++] = proto_object_allocate(&data);

            ptr += limit;
            offset += limit;
            remaining -= limit;
        }

        client_state_send_proto_objects(server_state, client_state, objects, msg_id);
    }
    else
    {
        declare_variable_property_on_stack(data, 'p', data_entry->payload + data_offset, payload_len, &id);

        client_state_send_proto_one_object(server_state, client_state, &data);
    }
}

void server_data_push_module(struct client_state_t* client_state, const char* name)
{
    struct server_state_t* server_state = client_state->state;
    struct server_data_entry_t* data_entry = find_data_entry(&server_state->server_data_modules, name);
    if (data_entry == NULL)
        return;

    client_printf(client_state, "pushing module %s\n", name);

    if (data_entry->payload_len < 4096)
    {
        server_data_push_module_namespace(client_state, data_entry, 0, data_entry->payload_len, "NAMESPACE0");
    }
    else
    {
        server_data_push_module_namespace(client_state, data_entry, 0, 4096, "NAMESPACE0");
        server_data_push_module_namespace(client_state, data_entry, 4096, data_entry->payload_len - 4096, "NAMESPACE1");
    }
}

void server_data_module_action(struct client_state_t* client_state, const char* name, PyObject* action)
{
    struct server_state_t* server_state = client_state->state;
    struct server_data_entry_t* data_entry = find_data_entry(&server_state->server_data_modules, name);
    if (data_entry == NULL)
        return;

    struct server_data_kv_t* namespace_p = get_data_entry_prop(data_entry, "NAMESPACE0");
    if (namespace_p->type != SERVER_KV_INT)
        return;

    uint8_t namespace = namespace_p->as_int;

    declare_arg_property_on_stack(_namespace, 'n', namespace, NULL);
    uint8_t command = MSG_MODULE_ACTION;
    declare_arg_property_on_stack(id, OBJ_PROPERTY_ID, command, &_namespace);

    ProtoObject** objects = calloc(1, sizeof(ProtoObject*));
    objects[0] = py_dict_to_proto_object(action, &id);
    client_state_send_proto_objects(server_state, client_state, objects, 1);
}