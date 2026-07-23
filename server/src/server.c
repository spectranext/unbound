
#ifdef WIN32
#include <WinSock2.h>
#else
#include <sys/socket.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "proto.h"
#include "proto_req.h"
#include <pthread.h>
#include <time.h>
#include <sys/un.h>

#include "block.h"
#include "object.h"
#include "req_handlers.h"
#include "server.h"
#include "computer.h"
#include "messages.h"
#include "server_map.h"
#include "server_python.h"
#include "debug_window.h"
#include "server_bullets.h"

uint64_t server_tick = 0;

static void server_state_flush_deleted_objects(struct server_state_t* state)
{
    struct server_object_delete_queue_t* elem = NULL;
    struct server_object_delete_queue_t* tmp = NULL;

    LL_FOREACH_SAFE(state->objects_to_delete, elem, tmp)
    {
        LL_DELETE(state->objects_to_delete, elem);
        map_delete_object(state, &state->map.map, elem->object_id);
        free(elem);
    }
}

int server_state_init(struct server_state_t *state, uint16_t width, uint16_t height, const char* scenario,
    int listen_port, const char *py_debug_host, int py_debug_port)
{
    server_map_init(&state->map, width, height);
    state->listen_port = listen_port;
    state->running = 1;
    state->scenario = strdup(scenario);
    state->next_client_id = 1;
    state->map_refresh_checks = 0;
    state->client_states = NULL;
    state->client_states_user_ids = NULL;
    state->client_states_ids = NULL;
    state->block_schedule_check = NULL;
    state->callback_schedule = NULL;
    state->objects_to_delete = NULL;
    state->map_save_timer = server_time() + 10000;
    state->cb_timer = server_time() + 50;
    state->map_update_timer = server_time() + 50;

    server_screens_init(state);

    if (computer_rom_load(&state->rom_48k, "pages/48k.rom"))
    {
        return 1;
    }

    if (computer_rom_load(&state->rom_spectranet, "pages/spectranet.rom"))
    {
        return 1;
    }

    if (server_data_init(&state->server_data, "pages/data"))
    {
        return 2;
    }

    if (server_data_init(&state->server_data_modules, "pages/modules"))
    {
        return 3;
    }

    {

        FILE* fp = fopen("pages/client.bin", "r");
        if (fp == NULL)
        {
            return 5;
        }

        fseek(fp, 0L, SEEK_END);
        state->client_binary_size = ftell(fp);
        rewind(fp);
        state->client_binary = malloc(state->client_binary_size);
        fread(state->client_binary, state->client_binary_size, 1, fp);
        fclose(fp);

        state->client_binary_addr = 25000;

        server_printf("Loaded client binary %d bytes at address %d\n",
            state->client_binary_size, state->client_binary_addr);
    }

    if (server_data_post_process(&state->server_data))
    {
        return 6;
    }

    if (server_python_init(state, &state->server_python, py_debug_host, py_debug_port))
    {
        return 7;
    }

    network_bindings_init(&state->network_bindings, state);

    pthread_mutex_init(&state->runnable_mutex, NULL);
    pthread_mutex_init(&state->computers_mutex, NULL);
    register_server_handlers(state);
    return 0;
}

void server_state_free(struct server_state_t* state)
{
    server_python_map_shutdown(&state->server_python);
    if (state->save_map)
    {
        server_map_save(state, &state->map, state->map_filename);
    }
    network_bindings_destroy(&state->network_bindings);
    server_state_flush_deleted_objects(state);
    server_python_free(&state->server_python);
    pthread_mutex_destroy(&state->runnable_mutex);
    pthread_mutex_destroy(&state->computers_mutex);
    server_map_free(&state->map);
}

void server_state_generate(struct server_state_t *state, const char* scenario)
{
    server_printf("generating new world %dx%d...\n", state->map.map.width, state->map.map.height);

    // zero it out first

    for (uint16_t j = 0; j < state->map.map.height; j++)
    {
        for (uint16_t i = 0; i < state->map.map.width; i++)
        {
            struct map_chunk_t* chunk = map_get_chunk((struct map_t*)&state->map, i, j);
            if (chunk == NULL)
                continue;

            map_chunk_fill(chunk, 0);
        }
    }

    server_python_map_generate(&state->server_python, scenario);

    server_python_map_refresh(&state->server_python);
    server_python_map_yield_chunks(state, &state->server_python);
}

static uint8_t server_state_client_state_init(struct client_state_t* state,
    struct server_state_t* server_state, int new_client)
{
    state->inited = 0;
    state->client_socket = new_client;
    state->state = server_state;
    state->sync_check_timer = 0;
    state->client_id = 0;
    state->client_object = 0;
    state->control_object = 0;
    strcpy(state->user_name, "?");
    state->total_chunks_syncing = 0;
    state->proto_send_jobs = NULL;
    memset(&state->synced_objects, 0xFF, sizeof(state->synced_objects));
    memset(&state->synced_chunks, 0, sizeof(state->synced_chunks));

    proto_init(&state->proto, state->proto_buffer, sizeof(state->proto_buffer));

    pthread_mutex_init(&state->proto_send_jobs_mutex, NULL);
    pthread_mutex_init(&state->post_wait.mutex, NULL);
    pthread_cond_init(&state->post_wait.cond, NULL);

    state->py_last_query_response = NULL;
    state->py_postponed_touch = NULL;

    LL_APPEND(server_state->client_states, state);

    client_printf(state, "new client\n");
    return 0;
}

void server_python_client_set_object_state(struct client_state_t* client_state, enum client_object_state_t state)
{
    if (client_state->control_object)
        return;

    struct server_map_t* map = &client_state->state->map;
    if (map)
    {
        struct server_object_reference_t* ref = server_map_get_object(map, client_state->client_object);

        if (ref)
        {
            map_set_object_state(client_state->state, &map->map, ref, state);
        }
    }
}

void server_python_client_set_object_state_default(struct client_state_t* client_state)
{
    if (client_state->control_object)
        return;

    struct server_map_t* map = &client_state->state->map;
    if (map)
    {
        struct server_object_reference_t* ref = server_map_get_object(map, client_state->client_object);

        if (ref)
        {
            enum client_object_state_t state = server_python_get_object_default_state(
                    &client_state->state->server_python, ref);
            
            map_set_object_state(client_state->state, &map->map, ref, state);
        }
    }
}

uint8_t server_state_client_find_synced_object(struct client_state_t* state, struct server_state_t* server_state,
    struct map_object_t* o)
{
    uint16_t object_id = o->object_id;

    for (uint8_t i = 0; i < MAX_CLIENT_CACHED_OBJECTS; i++)
    {
        if (state->synced_objects[i] == object_id)
        {
            return i;
        }
    }

    return 0xFF;
}

static void server_state_client_remove_object_sync_update(struct client_state_t* state, int object_id)
{
    struct client_object_sync_queue_t* queued = NULL;
    HASH_FIND_INT(state->object_sync_queue, &object_id, queued);

    if (queued)
    {
        HASH_DELETE(hh, state->object_sync_queue, queued);
        free(queued);
    }

    if (state->object_sync_queue == NULL)
    {
        state->object_sync_time = 0;
    }
}

static void server_state_client_sync_object(struct client_state_t* state, struct server_state_t* server_state,
    struct server_object_reference_t* ref)
{
    struct map_object_t* o = &ref->object;

    uint16_t object_id = o->object_id;

    {
        uint8_t synced_slot = server_state_client_find_synced_object(state, server_state, o);
        if (synced_slot != 0xFF)
        {
            // we've up to sync
            return;
        }
    }

    uint8_t free_slot = 0xFF;

    for (uint8_t i = 0; i < MAX_CLIENT_CACHED_OBJECTS; i++)
    {
        if (state->synced_objects[i] == 0xFFFF)
        {
            free_slot = i;
            break;
        }
    }

    if (free_slot != 0xFF)
    {
        client_printf(state, "syncing object %d on %dx%d\n", object_id,
            OBJECT_PHY_TO_LOGICAL(o->location.x), OBJECT_PHY_TO_LOGICAL(o->location.y));

        state->synced_objects[free_slot] = object_id;

        // notify the client

        declare_arg_property_on_stack(slot_id, 's', free_slot, NULL);
        declare_variable_property_on_stack(_st, '_', &o->object_id,
            (o->type & MAP_OBJECT_SPRITE) ?
            MAP_OBJECT_SYNC_CRITICAL_SIZE_W_SPRITE_OFFSET :
            MAP_OBJECT_SYNC_CRITICAL_SIZE, &slot_id);

        uint8_t command = MSG_SYNC_OBJ;
        declare_arg_property_on_stack(id, OBJ_PROPERTY_ID, command, &_st);
        client_state_send_proto_one_object(server_state, state, &id);
    }
}

void server_state_client_touch_progress(struct server_state_t* server_state,
    struct client_state_t* client_state, uint8_t progress)
{
    declare_arg_property_on_stack(_p, 'p', progress, NULL);
    uint8_t command = MSG_PROGRESS;
    declare_arg_property_on_stack(id, OBJ_PROPERTY_ID, command, &_p);
    client_state_send_proto_one_object(server_state, client_state, &id);
}


void server_state_client_unsync_object(struct client_state_t* state, struct server_state_t* server_state,
    struct map_object_t* o)
{
    server_state_client_remove_object_sync_update(state, o->object_id);

    uint8_t synced_slot = server_state_client_find_synced_object(state, server_state, o);

    if (synced_slot == 0xFF)
    {
        // that object is not synced
        return;
    }

    struct server_map_chunk_subscriber_t* sub =
        server_map_chunk_get_subscriber(
            &server_state->map,
            OBJECT_PHY_TO_LOGICAL_CHUNK(o->location.x),
            OBJECT_PHY_TO_LOGICAL_CHUNK(o->location.y), state);

    uint8_t sub_id = 0xFF;
    if (sub)
    {
        sub_id = sub->id;
    }

    client_printf(state, "unsyncing object %d on %dx%d\n", o->object_id,
        OBJECT_PHY_TO_LOGICAL(o->location.x), OBJECT_PHY_TO_LOGICAL(o->location.y));

    // unmark it
    state->synced_objects[synced_slot] = 0xFFFF;

    struct MSG_UNSYNC_OBJ_t msg = {
        .sync_chunk_id = sub_id,
        .object_id = o->object_id,
        .slot = synced_slot
    };

    // notify the client
    declare_arg_property_on_stack(_msg, '_', msg, NULL);
    uint8_t command = MSG_UNSYNC_OBJ;
    declare_arg_property_on_stack(id, OBJ_PROPERTY_ID, command, &_msg);

    client_state_send_proto_one_object(server_state, state, &id);
}

void server_state_client_send_effect(struct client_state_t* state, struct server_state_t* server_state,
    const char* effect, uint16_t x, uint16_t y)
{
    struct server_data_entry_t* e = find_data_entry(&server_state->server_data, effect);
    if (e == NULL)
    {
        return;
    }

    uint16_t chunk_x = OBJECT_PHY_TO_LOGICAL_CHUNK(x);
    uint16_t chunk_y = OBJECT_PHY_TO_LOGICAL_CHUNK(y);

    if (!server_map_chunk_get_subscriber(&server_state->map, chunk_x, chunk_y, state))
        return;

    uint8_t frames = get_data_entry_prop_int(e, "FRAMES", 0);
    uint8_t snd = get_data_entry_prop_int(e, "SND", 0xFF);
    uint8_t rate = get_data_entry_prop_int(e, "RATE", 0);
    uint8_t motion = get_data_entry_prop_int(e, "MOTION", 0);

    struct MSG_EFFECT_t msg = {
        .x = x,
        .y = y,
        .sound = snd,
        .data = {e->index, frames, rate, motion}
    };

    declare_arg_property_on_stack(_msg, '_', msg, NULL);
    uint8_t command = MSG_EFFECT;
    declare_arg_property_on_stack(id, OBJ_PROPERTY_ID, command, &_msg);

    client_state_send_proto_one_object(server_state, state, &id);
}

void server_state_client_check_watch(struct client_state_t* state, struct server_state_t* server_state)
{
    if (state->watch_active == 0)
        return;

    struct server_object_reference_t* ref = server_map_get_object(&server_state->map, server_state_client_active_object(state));
    if (ref == NULL)
        return;

    uint16_t camera_base_x = (uint16_t)state->watch_x * MAP_CHUNK_SIZE;
    uint16_t camera_base_y = (uint16_t)state->watch_y * MAP_CHUNK_SIZE;

    uint16_t watch_base_x = OBJECT_PHY_TO_LOGICAL(ref->object.location.x);
    uint16_t watch_base_y = OBJECT_PHY_TO_LOGICAL(ref->object.location.y);

    uint16_t new_watch_x = state->watch_x;
    uint16_t new_watch_y = state->watch_y;
    uint8_t update_watch = 0;

    if (watch_base_x < camera_base_x + 8)
    {
        if (state->watch_x >= 2)
        {
            new_watch_x -= 2;
            update_watch = 1;
        }
        else
        {
            if (state->watch_x > 0)
            {
                new_watch_x = 0;
                update_watch = 1;
            }
        }
    }

    if (watch_base_y < camera_base_y + 4)
    {
        if (state->watch_y > 2)
        {
            new_watch_y -= 1;
            update_watch = 1;
        }
        else
        {
            if (state->watch_y > 0)
            {
                new_watch_y = 0;
                update_watch = 1;
            }
        }
    }

    if (watch_base_x >= camera_base_x + 24)
    {
        new_watch_x += 2;
        update_watch = 1;
    }

    if (watch_base_y > camera_base_y + 24 - 4)
    {
        new_watch_y += 1;
        update_watch = 1;
    }

    if (new_watch_x > (server_state->map.map.width - 4))
    {
        new_watch_x = server_state->map.map.width - 4;
    }

    if (update_watch)
    {
        server_state_client_set_watch(state, server_state, new_watch_x, new_watch_y);
    }
}

static void client_state_sync_chunks(struct server_state_t* server_state, struct client_state_t* client_state);
static void server_state_object_set_client_id(struct client_state_t* state, uint16_t object_id, uint16_t client_id);
static void server_state_force_watch(struct client_state_t* state, uint16_t object_id);

void server_state_client_set_watch(struct client_state_t* state, struct server_state_t* server_state,
    uint8_t chunk_x, uint8_t chunk_y)
{
    if ((state->watch_active == 0) || (state->watch_x != chunk_x) || (state->watch_y != chunk_y))
    {
        client_printf(state, "watch %d,%d\n", chunk_x, chunk_y);
        state->watch_x = chunk_x;
        state->watch_y = chunk_y;

        client_printf(state, "moving watch to %dx%d\n", state->watch_x, state->watch_y);

        struct MSG_WATCH_t msg = {
            .x = state->watch_x,
            .y = state->watch_y
        };

        uint8_t chunk_id = 0;
        for (uint8_t y = 0; y < 3; y++)
        {
            for (uint8_t x = 0; x < 4; x++)
            {
                struct server_map_chunk_subscriber_t* sub =
                    server_map_chunk_get_subscriber(
                        &server_state->map,
                        state->watch_x + x,
                        state->watch_y + y, state);

                if (sub)
                {
                    msg.chunks[chunk_id++] = sub->id;
                }
                else
                {
                    msg.chunks[chunk_id++] = 0xFF;
                }
            }
        }

        declare_arg_property_on_stack(_msg, '_', msg, NULL);
        uint8_t command = MSG_WATCH;
        declare_arg_property_on_stack(id, OBJ_PROPERTY_ID, command, &_msg);

        client_state_send_proto_one_object(server_state, state, &id);

        state->watch_active = 1;
    }
}

void server_state_client_force_query_result(struct client_state_t* state, struct server_state_t* server_state,
    struct player_query_result* result)
{
    uint8_t options_count = 0;
    {
        struct player_query_option_t* option = result->options;
        while (option)
        {
            option = option->next;
            options_count++;
        }
    }

    ProtoObject** objects = malloc(sizeof(ProtoObject*) * (options_count + 1));


    // first object has a bunch of actions
    {
        ProtoStackObjectProperty actions[result->actions_count];
        memset(&actions, 0, sizeof(actions));
        ProtoStackObjectProperty* prev = NULL;

        struct player_query_action_t* action = result->actions;
        size_t i = 0;
        while (action)
        {
            ProtoStackObjectProperty* prop = &actions[i++];
            prop->key = 'a';
            prop->value = action->action;
            prop->value_size = strlen(action->action);

            prop->prev = prev;
            prev = prop;
            action = action->next;
        }

        uint8_t primary = 0;
        uint8_t secondary = 0;

        {
            struct player_query_option_t* option = result->options;
            while (option)
            {
                if (option->secondary)
                {
                    secondary = 1;
                }
                else
                {
                    primary = 1;
                }
                option = option->next;
            }
        }

        if (secondary && (!primary))
        {
            struct player_query_option_t* option = result->options;
            while (option)
            {
                option->secondary = 0;
                option = option->next;
            }

            secondary = 0;
        }

        declare_str_property_on_stack(cancel_action, 'x', result->cancel_action, prev);
        declare_str_property_on_stack(message, 'm', result->message, &cancel_action);
        uint8_t cc = result->current_option;
        declare_arg_property_on_stack(current_option, 'c', cc, &message);

        declare_arg_property_on_stack(_secondary, 's', secondary, &current_option);
        declare_arg_property_on_stack(edit, 'e', result->edit, &_secondary);
        declare_arg_property_on_stack(quick_cancel, 'q', result->quick_cancel, &edit);

        if (result->description)
        {
            declare_str_property_on_stack(desctiption, 'd', result->description, &quick_cancel);
            declare_variable_property_on_stack(image, 'I', result->image, result->image_size, &desctiption);
            declare_arg_property_on_stack(flags, 'f', result->flags, &image);
            uint8_t command = MSG_FORCE_QUERY_RESULT;
            declare_arg_property_on_stack(id, OBJ_PROPERTY_ID, command, &flags);
            objects[0] = proto_object_allocate(&id);
        }
        else
        {
            declare_variable_property_on_stack(image, 'I', result->image, result->image_size, &quick_cancel);
            declare_arg_property_on_stack(flags, 'f', result->flags, &image);
            uint8_t command = MSG_FORCE_QUERY_RESULT;
            declare_arg_property_on_stack(id, OBJ_PROPERTY_ID, command, &flags);
            objects[0] = proto_object_allocate(&id);
        }
    }

    {
        struct player_query_option_t* option = result->options;
        uint8_t i = 0;
        while (option)
        {
            declare_arg_property_on_stack(id, 'i', i, NULL);
            declare_arg_property_on_stack(icon, 'c', option->icon, &id);
            declare_arg_property_on_stack(full_icon, 'c', option->full_icon, &id);
            declare_arg_property_on_stack(secondary, 's', option->secondary, option->has_full_icon ? &full_icon : &icon);
            declare_str_property_on_stack(o, 'o', option->option, &secondary);
            objects[i + 1] = proto_object_allocate(&o);
            option = option->next;
            i++;
        }
    }

    client_state_send_proto_objects(server_state, state, objects, options_count + 1);
}

extern void server_state_client_sync_state(struct client_state_t* state, struct server_state_t* server_state,
    struct map_object_t* o)
{
    uint8_t synced_slot = server_state_client_find_synced_object(state, server_state, o);

    if (synced_slot == 0xFF)
    {
        // that object is not synced
        return;
    }

    client_printf(state, "syncing object state %d on %dx%d\n", o->object_id,
        OBJECT_PHY_TO_LOGICAL(o->location.x), OBJECT_PHY_TO_LOGICAL(o->location.y));

    struct MSG_OBJ_STATE_t msg = {
        .object_slot = synced_slot,
        .state = o->state,
        .state_flags = o->state_flags
    };

    // notify the client
    declare_arg_property_on_stack(_msg, '_', msg, NULL);
    uint8_t command = MSG_OBJ_STATE;
    declare_arg_property_on_stack(id, OBJ_PROPERTY_ID, command, &_msg);

    client_state_send_proto_one_object(server_state, state, &id);
}

void server_state_client_sync_stats(struct client_state_t* state, struct server_state_t* server_state)
{
    struct server_object_reference_t* ref = server_map_get_object(&state->state->map,
        server_state_client_active_object(state));

    if (ref == NULL)
        return;

    client_printf(state, "syncing object stats on object %d\n", ref->object.object_id);

    uint8_t power;
    uint8_t temperature;
    uint8_t hit_auto;
    uint8_t hit_delay;
    uint8_t health;
    uint16_t credits;
    char default_state[128];
    char building_state[128];

    server_python_player_get_stats(&server_state->server_python, state, &health, &power, &temperature,
        &hit_auto, &hit_delay, &credits, default_state, building_state);

    // notify the client
    declare_arg_property_on_stack(_health, 'h', health, NULL);
    declare_arg_property_on_stack(_temperature, 't', temperature, &_health);
    declare_arg_property_on_stack(_power, 'p', power, &_temperature);
    declare_arg_property_on_stack(_hit_auto, 'a', hit_auto, &_power);
    declare_arg_property_on_stack(_credits, 'c', credits, &_hit_auto);
    declare_arg_property_on_stack(_hit_delay, 'd', hit_delay, &_credits);
    declare_str_property_on_stack(_default_state, '1', default_state, &_hit_delay);
    declare_str_property_on_stack(_building_state, '2', building_state, &_default_state);
    uint8_t command = MSG_YOUR_STATS;
    declare_arg_property_on_stack(id, OBJ_PROPERTY_ID, command, &_building_state);

    client_state_send_proto_one_object(server_state, state, &id);
}

static void client_state_unsubscribe_map(struct server_state_t* server_state, struct client_state_t* client_state)
{
    uint8_t had_syncing = client_state->total_chunks_syncing;

    for (uint8_t y = 0; y < server_state->map.map.height; y++)
    {
        for (uint8_t x = 0; x < server_state->map.map.width; x++)
        {
            if (!server_map_chunk_get_subscriber(&server_state->map, x, y, client_state))
                continue;

            server_map_chunk_unsubscribe(&server_state->map, x, y, client_state);
            --client_state->total_chunks_syncing;
        }
    }

    client_printf(client_state, "unsubscribed map, had %d, now have %d\n",
        had_syncing, client_state->total_chunks_syncing);
}

static void server_state_client_state_free(struct client_state_t* state)
{
    {
        char msg[200];
        sprintf(msg, "Player %s disconnected.", state->user_name);

        struct client_state_t* client_state;
        LL_FOREACH(state->state->client_states, client_state)
        {
            if (client_state != state)
            {
                client_state_notify_message(state->state, client_state, msg,
                    NOTIFY_MESSAGE_COLOR_BRIGHT);
            }
        }
    }

    // we need to unsubscribe before deleting player's object
    client_state_unsubscribe_map(state->state, state);

    // release name from the table
    client_state_release_name(state->state, state);
    client_state_release_id(state->state, state);

    // remove from states before deleting object, so the client itself won't receive a notification
    // about the deletion
    LL_DELETE(state->state->client_states, state);

    // Cache/free the Python client while the player object still exists, so
    // disconnect recovery can serialize the player position and state.
    server_python_client_free(&state->state->server_python, state);

    struct server_object_reference_t* ref = server_map_get_object(&state->state->map, state->client_object);
    if (ref)
    {
        map_delete_object(state->state, &state->state->map.map, ref->object.object_id);
    }

    pthread_mutex_destroy(&state->proto_send_jobs_mutex);
    pthread_mutex_destroy(&state->post_wait.mutex);
    pthread_cond_destroy(&state->post_wait.cond);

    close(state->client_socket);
    client_printf(state, "client destroyed\n");

    free(state);
}

static const char* client_disconnect_main_thread(struct server_main_thread_runnable_args* args)
{
    server_state_client_state_free(args->state);
    return NULL;
}

static void optimize_send_jobs(struct client_state_t* args, uint8_t property_id)
{
    uint8_t do_over = 1;

    while (do_over)
    {
        do_over = 0;

        uint16_t batch_object_count = 0;
        uint16_t batch_size = 0;
        uint16_t requests_count = 0;

        struct client_proto_send_job_t* job;
        struct client_proto_send_job_t* tmp;

        // figure out how many objects can fit into this batch
        LL_FOREACH_SAFE(args->proto_send_jobs, job, tmp)
        {
            if (job->optimized)
                continue;

            uint8_t p = get_uint8_property(job->objects[0], OBJ_PROPERTY_ID, 0xFF);

            if (p != property_id)
                continue;

            uint16_t this_size = proto_serialize_get_size(job->objects, job->amount);
            if (batch_size + this_size > 2048)
            {
                do_over = 1;
                break;
            }

            batch_size += this_size;
            batch_object_count += job->amount;
            requests_count++;
        }

        if (requests_count <= 1)
            break;

        // we need to merge some requests
        ProtoObject** batch_objects = calloc(batch_object_count, sizeof(ProtoObject*));
        uint16_t object_count = 0;

        LL_FOREACH_SAFE(args->proto_send_jobs, job, tmp)
        {
            if (job->optimized)
                continue;

            uint8_t p = get_uint8_property(job->objects[0], OBJ_PROPERTY_ID, 0xFF);

            if (p != property_id)
                continue;

            LL_DELETE(args->proto_send_jobs, job);

            for (int i = 0; i < job->amount; i++)
            {
                batch_objects[object_count++] = job->objects[i];
            }

            free(job->objects);
            free(job);

            if (object_count >= batch_object_count)
            {
                break;
            }
        }

        struct client_proto_send_job_t* n = calloc(1, sizeof(struct client_proto_send_job_t));
        n->objects = batch_objects;
        n->amount = batch_object_count;
        n->optimized = 1;
        LL_APPEND(args->proto_send_jobs, n);
    }
}

static void* server_state_client_loop(void* a)
{
    struct client_state_t* args = (struct client_state_t*)a;
    struct proto_req_processor_t req_handle = {};
    req_handle.user = args;

    register_client_handlers(&req_handle, args);

    int ret;
    while ((ret = proto_req_server_process(args->client_socket, &args->proto, &req_handle)) == 0)
    {
        usleep(1000);

        pthread_mutex_lock(&args->proto_send_jobs_mutex);

        if (args->proto_send_jobs)
        {
            struct client_proto_send_job_t* job;
            struct client_proto_send_job_t* tmp;

            uint32_t total_send_size = 0;
            uint8_t property_a = 0xFF;
            uint8_t property_b = 0xFF;
            uint16_t count_a = 0;
            uint16_t count_b = 0;
            uint32_t total_object_count = 0;

            LL_FOREACH(args->proto_send_jobs, job)
            {
                total_send_size += proto_serialize_get_size(job->objects, job->amount);
                total_object_count += job->amount;

                if (job->amount == 1)
                {
                    uint8_t p = get_uint8_property(job->objects[0], OBJ_PROPERTY_ID, 0xFF);

                    if (property_a == 0xFF)
                    {
                        property_a = p;
                        count_a = 1;
                    }
                    else
                    {
                        if (p == property_a)
                        {
                            count_a++;
                        }
                        else
                        {
                            if (property_b == 0xFF)
                            {
                                property_b = p;
                                count_b = 1;
                            }
                            else
                            {
                                if (p == property_b)
                                {
                                    count_b++;
                                }
                            }
                        }
                    }
                }
            }

            if (count_a > 1 && (property_a != 0xFF))
            {
                optimize_send_jobs(args, property_a);
            }

            if (count_b > 1 && (property_b != 0xFF))
            {
                optimize_send_jobs(args, property_b);
            }

            total_send_size = 0;

            LL_FOREACH(args->proto_send_jobs, job)
            {
                total_send_size += proto_serialize_get_size(job->objects, job->amount);
            }

            uint8_t* send_buffer = malloc(total_send_size);
            uint8_t* send_ptr = send_buffer;

            LL_FOREACH_SAFE(args->proto_send_jobs, job, tmp)
            {
                LL_DELETE(args->proto_send_jobs, job);
                send_ptr = proto_serialize(send_ptr, job->objects, job->amount, 0, 0);

                for (uint8_t i = 0; i < job->amount; i++)
                {
                    free(job->objects[i]);
                }
                free(job->objects);
                free(job);
            }

            send(args->client_socket, send_buffer, total_send_size, 0);
            free(send_buffer);
        }

        pthread_mutex_unlock(&args->proto_send_jobs_mutex);
    }

    client_printf(args, "proto_req_server_process returned %d\n", ret);

    {
        struct server_main_thread_runnable_args free_args;
        free_args.state = args;
        server_state_post_runnable(args->state, client_disconnect_main_thread, free_args);
    }

    return NULL;
}

static void* server_state_accept_loop(void* a)
{
    struct server_state_t* state = (struct server_state_t*)a;

    struct timeval tv;

    tv.tv_sec = 0;
    tv.tv_usec = 50000;

    while (state->running)
    {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(state->accept_socket, &rfds);

        int accepted = select(state->accept_socket + 1, &rfds, NULL, NULL, &tv);

        if(accepted <= 0)
            continue;

        int new_client = accept(state->accept_socket, (struct sockaddr*)NULL, NULL);

        if (new_client < 0)
            continue;

        struct client_state_t* args = calloc(1, sizeof(struct client_state_t));

        server_state_client_state_init(args, state, new_client);

        pthread_create(&args->thread, NULL, server_state_client_loop, args);
    }

    return NULL;
}

static void server_state_process_runnables(struct server_state_t* state)
{
    pthread_mutex_lock(&state->runnable_mutex);

    {
        struct server_main_thread_runnable* elem;
        struct server_main_thread_runnable* tmp;

        LL_FOREACH_SAFE(state->mt_runnable, elem, tmp)
        {
            const char* error = elem->callback(&elem->args);

            if (elem->wait)
            {
                pthread_mutex_lock(&elem->wait->mutex);
                elem->wait->complete = 1;
                elem->wait->error = error;
                pthread_cond_signal(&elem->wait->cond);
                pthread_mutex_unlock(&elem->wait->mutex);
            }

            LL_DELETE(state->mt_runnable, elem);
            free(elem);
        }
    }

    pthread_mutex_unlock(&state->runnable_mutex);
}

static void server_state_update(struct server_state_t* state)
{
    long time = server_time();
    static long server_time_start = 0;

    if (server_time_start == 0)
    {
        server_time_start = time;
    }

    server_tick = time - server_time_start;

    {
        struct client_state_t* client_state;
        LL_FOREACH(state->client_states, client_state)
        {
            client_state_update(state, client_state);
        }
    }

    if (time > state->objects_updated_timer)
    {
        state->objects_updated_timer = time + 20;
        struct server_object_reference_t *ref, *tmp;
        HASH_ITER(hh, state->map.objects, ref, tmp)
        {
            if (ref->destroyed)
            {
                continue;
            }

            if (ref->object.type & MAP_OBJECT_COLLIDER)
            {
                continue;
            }

            update_server_object(state, ref);
            server_python_update_object_py(&state->server_python, ref);
        }

        server_bullets_update(state);
        server_state_flush_deleted_objects(state);
    }

    if (time > state->client_update_timer)
    {
        state->client_update_timer = time + 50;

        struct client_state_t* client_state;
        LL_FOREACH(state->client_states, client_state)
        {
            server_python_update(&state->server_python, client_state);
        }
    }

    {
        struct server_object_reference_t *ref, *tmp;
        HASH_ITER(hh, state->map.objects, ref, tmp)
        {
            if (ref->destroyed)
            {
                continue;
            }

            sync_server_object(state, ref);

            if (ref->object.type & MAP_OBJECT_COLLIDER)
            {
                update_server_object(state, ref);
                server_python_update_object_py(&state->server_python, ref);
            }
        }

        server_state_flush_deleted_objects(state);
    }

    if (state->save_map && time > state->map_save_timer)
    {
        state->map_save_timer = time + 30000;
        server_map_save(state, &state->map, state->map_filename);
    }

    {
        struct block_method_schedule_t* schedule = NULL;
        struct block_method_schedule_t* tmp = NULL;
        LL_FOREACH_SAFE(state->block_schedule_check, schedule, tmp)
        {
            if (time > schedule->time)
            {
                server_python_map_call_block_method(&state->map.map, schedule->x, schedule->y, schedule->method);
                LL_DELETE(state->block_schedule_check, schedule);
                free(schedule->method);
                free(schedule);
            }
        }
    }

    if (time > state->cb_timer)
    {
        state->cb_timer = time + 50;

        struct callback_method_schedule_t* schedule = NULL;
        struct callback_method_schedule_t* tmp = NULL;
        LL_FOREACH_SAFE(state->callback_schedule, schedule, tmp)
        {
            if (time > schedule->time)
            {
                server_python_map_call(schedule->callback);
                LL_DELETE(state->callback_schedule, schedule);
                free(schedule);
            }
        }
    }

    if (time > state->map_update_timer)
    {
        state->map_update_timer = time + 50;
        server_python_map_update(&state->server_python);
    }

    if (state->map_refresh_checks)
    {
        if (time > state->map_refresh_checks)
        {
            if (server_state_refresh_map(state))
            {
                state->map_refresh_checks = 0;
            }
        }
    }
}

void server_state_schedule_map_refresh(struct server_state_t* state, uint16_t delay)
{
    if (state->map_refresh_checks)
        return;

    server_printf("Scheduled map refresh\n");

    state->map_refresh_checks = server_time() + delay;
}

void server_state_cancel_schedule_block_method(struct server_state_t* state, uint16_t x, uint16_t y)
{
    struct block_method_schedule_t* el;
    struct block_method_schedule_t* tmp;

    LL_FOREACH_SAFE(state->block_schedule_check, el, tmp)
    {
        if (el->x == x && el->y == y)
        {
            LL_DELETE(state->block_schedule_check, el);
            free(el->method);
            free(el);
        }
    }
}

void server_state_schedule_block_method(struct server_state_t* state, uint16_t x, uint16_t y, uint16_t time, const char* method)
{
    struct block_method_schedule_t* schedule = calloc(1, sizeof(struct block_method_schedule_t));

    schedule->time = server_time() + time;
    schedule->x = x;
    schedule->y = y;
    schedule->method = strdup(method);

    LL_APPEND(state->block_schedule_check, schedule);
}

void server_state_schedule_callback(struct server_state_t* state, PyObject* callback, uint16_t time)
{
    struct callback_method_schedule_t* schedule = calloc(1, sizeof(struct callback_method_schedule_t));

    Py_IncRef(callback);
    schedule->time = server_time() + time;
    schedule->callback = callback;

    LL_APPEND(state->callback_schedule, schedule);
}

uint8_t server_state_refresh_map(struct server_state_t* state)
{
    state->map_refresh_count++;

    uint8_t result = server_python_map_refresh(&state->server_python);
    if (result == 0)
    {
        return 0;
    }

    server_printf("map refresh done, %d iterations\n", state->map_refresh_count);
    state->map_refresh_count = 0;
    return 1;
}

uint8_t server_state_listen(struct server_state_t* state)
{
#ifdef WIN32
    int iResult;
    WSADATA wsaData;
    iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (iResult != 0) {
        server_printf("WSAStartup failed: %d\n", iResult);
        return 1;
    }
#endif

    if ((state->accept_socket = proto_listen(state->listen_port)) <= 0)
    {
        server_printf("Could not listen: %d\n", state->accept_socket);
        return 2;
    }

    server_printf("listening on port %d for new connections...\n", state->listen_port);
    return 0;
}

void server_state_loop(struct server_state_t* state)
{
    server_printf("starting main loop...\n");

    pthread_t accept_thread;
    pthread_create(&accept_thread, NULL, server_state_accept_loop, state);

    while (state->running)
    {
        long time = server_time();

        server_state_process_runnables(state);

        long diff = server_time() - time;

        if (diff > 10)
        {
            printf("server_state_process_runnables took %ld\n", diff);
        }

        server_state_update(state);
        if (state->debug_window)
        {
            debug_window_frame();
        }
        usleep(10);
    }

    server_printf("Shutting down...\n");
    shutdown(state->accept_socket, 0);

    pthread_join(accept_thread, NULL);
}

void server_state_post_runnable(struct server_state_t* state, main_thread_runnable_cb callback, struct server_main_thread_runnable_args args)
{
    pthread_mutex_lock(&state->runnable_mutex);

    struct server_main_thread_runnable* entry = calloc(1, sizeof(struct server_main_thread_runnable));
    entry->callback = callback;
    entry->args = args;
    entry->wait = NULL;

    LL_APPEND(state->mt_runnable, entry);

    pthread_mutex_unlock(&state->runnable_mutex);
}

const char* server_state_post_runnable_wait(struct server_state_t* state, main_thread_runnable_cb callback,
    struct server_main_thread_runnable_args args, struct server_main_thread_runnable_wait* wait)
{
    pthread_mutex_lock(&state->runnable_mutex);

    struct server_main_thread_runnable* entry = calloc(1, sizeof(struct server_main_thread_runnable));
    entry->callback = callback;
    entry->args = args;
    entry->wait = wait;

    wait->complete = 0;
    wait->error = NULL;

    LL_APPEND(state->mt_runnable, entry);
    pthread_mutex_unlock(&state->runnable_mutex);

    pthread_mutex_lock(&wait->mutex);

    while (wait->complete == 0)
        pthread_cond_wait(&wait->cond, &wait->mutex);

    pthread_mutex_unlock(&wait->mutex);
    return wait->error;
}

extern long server_time()
{
#ifdef WIN32
    long time = GetTickCount();
#else
    struct timespec spec;
    if (clock_gettime(CLOCK_REALTIME, &spec) == -1)
    {
        abort();
    }

    long time = spec.tv_sec * 1000 + spec.tv_nsec / 1e6;
#endif

    if (get_server_state()->debug_window)
    {
        return debug_window_process_time(time);
    }

    return time;
}

static uint8_t is_client_chunk_on_screen(struct client_state_t* client_state, uint8_t ix, uint8_t iy)
{
    uint8_t chunk_x = client_state->watch_x;
    uint8_t chunk_y = client_state->watch_y;

    if (ix < chunk_x)
        return 0;

    if (iy < chunk_y)
        return 0;

    if (ix >= chunk_x + 4)
        return 0;

    if (iy >= chunk_y + 3)
        return 0;

    return 1;
}

static uint8_t client_supposed_sync_chunk(struct client_state_t* client_state, uint8_t ix, uint8_t iy,
    uint8_t syncing_already)
{
    uint8_t chunk_x = client_state->watch_x;
    uint8_t chunk_y = client_state->watch_y;

    int gap_x = syncing_already ? 4 : 0;
    int gap_y = syncing_already ? 3 : 0;

    if (ix + gap_x < chunk_x)
        return 0;

    if (iy + gap_y < chunk_y)
        return 0;

    if (ix >= chunk_x + gap_x + 4)
        return 0;

    if (iy >= chunk_y + gap_y + 3)
        return 0;

    return 1;
}

static uint8_t client_supposed_sync_chunk_objects(struct server_state_t* state, struct client_state_t* client_state,
    struct server_object_reference_t* ref)
{
    {
        uint8_t ix = OBJECT_PHY_TO_LOGICAL(ref->object.location.x) / MAP_CHUNK_SIZE;
        uint8_t iy = OBJECT_PHY_TO_LOGICAL(ref->object.location.y) / MAP_CHUNK_SIZE;

        uint8_t sx = client_state->watch_x;
        uint8_t sy = client_state->watch_y;

        if (ix < sx)
            return 0;

        if (iy < sy)
            return 0;

        if (ix >= sx + 4)
            return 0;

        if (iy >= sy + 3)
            return 0;
    }

    if (ref->object.team_id != client_state->team_id)
    {
        uint16_t ix = OBJECT_PHY_TO_LOGICAL(ref->object.location.x);
        uint16_t iy = OBJECT_PHY_TO_LOGICAL(ref->object.location.y);

        uint8_t light = map_get_light(&state->map.map, ix, iy);
        if (light == 0)
            return 0;
    }

    return 1;
}

static void client_state_sync_chunks(struct server_state_t* server_state, struct client_state_t* client_state)
{
    if (client_state->watch_active == 0)
        return;

    for (uint8_t y = 0; y < server_state->map.map.height; y++)
    {
        for (uint8_t x = 0; x < server_state->map.map.width; x++)
        {
            struct server_map_chunk_t* chunk = server_map_get_chunk(&server_state->map.map, x, y);

            struct server_map_chunk_subscriber_t* syncing = server_map_chunk_get_subscriber(&server_state->map, x, y, client_state);
            uint8_t supposed_to_sync = client_supposed_sync_chunk(client_state, x, y, (syncing ? 1 : 0));

            if (supposed_to_sync != (syncing ? 1 : 0))
            {
                uint16_t xy = x | (y << 8);

                if (supposed_to_sync)
                {
                    // we have to sync but don't
                    struct server_map_chunk_subscriber_t* sub = server_map_chunk_subscribe(&server_state->map, x, y, client_state);
                    if (sub == NULL)
                        continue;
                    uint8_t next_id = sub->id;

                    ++client_state->total_chunks_syncing;

                    client_printf(client_state, "need to sync chunk [%d] %dx%d, now %d\n", next_id, x, y,
                                  client_state->total_chunks_syncing);

                    if (client_state->total_chunks_syncing == CLIENT_MAX_SYNCED_CHUNKS)
                    {
                        client_printf(client_state, "Allocated last chunk to sync\n");
                    }

                    struct MSG_SYNC_t m;
                    m.next_id = next_id;
                    m.x = x;
                    m.y = y;
                    memcpy(m.chunk_data, chunk->chunk.data, MAP_CHUNK_SIZE_DATA_SQ);

                    // send map data first
                    declare_arg_property_on_stack(_m, '_', m, NULL);
                    uint8_t command = MSG_SYNC;
                    declare_arg_property_on_stack(id, OBJ_PROPERTY_ID, command, &_m);

                    client_state_send_proto_one_object(server_state, client_state, &id);
                }
                else
                {
                    // we're syncing but don't have to
                    uint8_t was_id = server_map_chunk_unsubscribe(&server_state->map, x, y, client_state);
                    if (was_id == 0xFF)
                        continue;

                    --client_state->total_chunks_syncing;

                    client_printf(client_state, "need to unsync chunk [%d] %dx%d, now %d\n", was_id, x, y,
                                  client_state->total_chunks_syncing);

                    declare_arg_property_on_stack(_id, 'i', was_id, NULL);
                    uint8_t command = MSG_UNSYNC;
                    declare_arg_property_on_stack(id, OBJ_PROPERTY_ID, command, &_id);

                    client_state_send_proto_one_object(server_state, client_state, &id);
                }
            }
            else
            {
                if (syncing && syncing->dirty)
                {
                    syncing->delay++;
                    if (syncing->delay < 4)
                        continue;
                    syncing->delay = 0;
                    syncing->dirty = 0;

                    struct server_map_chunk_subscriber_outgoing_block_t* modified;
                    struct server_map_chunk_subscriber_outgoing_block_t* tmp;

                    /*
                    if ((syncing->modified_blocks_count == 0) || (syncing->modified_blocks_count >= 8))
                    {
                        // explicit request or 8+ block updates, sync the whole chunk instead

                        uint16_t xy = x | (y << 8);

                        client_printf(client_state, "need to re-sync chunk %dx%d (%d blocks)\n", x, y, syncing->modified_blocks_count);

                        // send map data first
                        declare_arg_property_on_stack(_id, 'i', syncing->id, NULL);
                        declare_arg_property_on_stack(coord_xy, 'x', xy, &_id);
                        declare_variable_property_on_stack(
                            chunk_data, 'd',
                            chunk->chunk.data, MAP_CHUNK_SIZE_DATA_SQ, &coord_xy);
                        uint8_t command = MSG_SYNC;
                        declare_arg_property_on_stack(id, OBJ_PROPERTY_ID, command, &chunk_data);

                        client_state_send_proto_one_object(server_state, client_state, &id);
                    }
                    else
                     */
                    {
                        // less than 8 blocks, sync each one

                        client_printf(client_state, "need to sync %d blocks on chunk %dx%d\n",
                            syncing->modified_blocks_count, x, y);

                        HASH_ITER(hh, syncing->modified_blocks, modified, tmp)
                        {
                            if (is_client_chunk_on_screen(client_state, x, y))
                            {
                                struct MSG_BLOCK_ON_SCREEN_t msg = {
                                    .id = syncing->id,
                                    .x = modified->x,
                                    .y = modified->y,
                                    .code = modified->code,
                                    .screen_x = modified->x + MAP_CHUNK_SIZE * (x - client_state->watch_x),
                                    .screen_y = modified->y + MAP_CHUNK_SIZE * (y - client_state->watch_y)
                                };

                                declare_arg_property_on_stack(_msg, '_', msg, NULL);

                                uint8_t command = MSG_BLOCK_ON_SCREEN;
                                declare_arg_property_on_stack(id, OBJ_PROPERTY_ID, command, &_msg);

                                client_state_send_proto_one_object(server_state, client_state, &id);
                            }
                            else
                            {
                                struct MSG_BLOCK_OFF_SCREEN_t msg = {
                                    .id = syncing->id,
                                    .x = modified->x,
                                    .y = modified->y,
                                    .code = modified->code
                                };

                                declare_arg_property_on_stack(_msg, '_', msg, NULL);

                                uint8_t command = MSG_BLOCK_OFF_SCREEN;
                                declare_arg_property_on_stack(id, OBJ_PROPERTY_ID, command, &_msg);

                                client_state_send_proto_one_object(server_state, client_state, &id);
                            }
                        }
                    }

                    HASH_ITER(hh, syncing->modified_blocks, modified, tmp)
                    {
                        HASH_DELETE(hh, syncing->modified_blocks, modified);
                        free(modified);
                    }

                    syncing->modified_blocks_count = 0;
                }
            }
        }
    }

    for (uint8_t i = 0; i < CLIENT_MAX_SYNCED_CHUNKS; i++)
    {
        // clean dirty
        if (client_state->synced_chunks[i] == 0xFF)
        {
            client_state->synced_chunks[i] = 0;
        }
    }
}

static void client_state_sync(struct server_state_t* server_state, struct client_state_t* client_state)
{
    client_state_sync_chunks(server_state, client_state);

    // iterate over objects to see if the player has to know about it or not

    if (client_state->watch_active)
    {
        struct server_object_reference_t *s, *tmp;
        HASH_ITER(hh, server_state->map.objects, s, tmp)
        {
            uint8_t chunk_x = OBJECT_PHY_TO_LOGICAL(s->object.location.x) / MAP_CHUNK_SIZE;
            uint8_t chunk_y = OBJECT_PHY_TO_LOGICAL(s->object.location.y) / MAP_CHUNK_SIZE;

            if (server_map_chunk_get_subscriber(&server_state->map, chunk_x, chunk_y, client_state) &&
                client_supposed_sync_chunk_objects(server_state, client_state, s))
            {
                server_state_client_sync_object(client_state, server_state, s);
            }
            else
            {
                server_state_client_unsync_object(client_state, server_state, &s->object);
            }
        }
    }
}

void client_state_send_proto_objects(struct server_state_t* server_state, struct client_state_t* client_state,
    ProtoObject** objects, uint8_t amount)
{
    pthread_mutex_lock(&client_state->proto_send_jobs_mutex);

    struct client_proto_send_job_t* job = calloc(1, sizeof(struct client_proto_send_job_t));
    job->objects = objects;
    job->amount = amount;

    LL_APPEND(client_state->proto_send_jobs, job);

    pthread_mutex_unlock(&client_state->proto_send_jobs_mutex);
}

void client_state_notify_message(struct server_state_t* server_state, struct client_state_t* client_state,
    const char* message, enum notify_message_color_t color)
{
    declare_str_property_on_stack(_m, 'm', message, NULL);
    uint8_t cc = color;
    declare_arg_property_on_stack(_c, 'c', cc, &_m);
    uint8_t command = MSG_NOTIFY;
    declare_arg_property_on_stack(id, OBJ_PROPERTY_ID, command, &_c);

    client_state_send_proto_one_object(server_state, client_state, &id);
}

void client_state_send_proto_one_object(struct server_state_t* server_state, struct client_state_t* client_state,
    ProtoStackObjectProperty* last_property)
{

    ProtoObject** objects = calloc(1, sizeof(ProtoObject*));
    objects[0] = proto_object_allocate(last_property);
    uint8_t id = get_uint8_property(objects[0], OBJ_PROPERTY_ID, '*');
    client_state_send_proto_objects(server_state, client_state, objects, 1);
}

void client_state_assign_user_id(struct server_state_t* server_state, struct client_state_t* client_state,
    const char* user_id, const char* user_name)
{
    strcpy(client_state->user_id, user_id);
    strcpy(client_state->user_name, user_name);
    client_printf(client_state, "assigned user_id %s user_name %s\n", client_state->user_id, client_state->user_name);
    HASH_ADD(hh_name, server_state->client_states_user_ids, user_id, strlen(user_id), client_state);
}

void client_state_assign_client_id(struct server_state_t* server_state, struct client_state_t* client_state, uint16_t client_id)
{
    client_state->client_id = client_id;
    HASH_ADD(hh_id, server_state->client_states_ids, client_id, sizeof(client_id), client_state);
}

struct client_state_t* client_state_find_user_id(struct server_state_t* server_state, const char* user_id)
{
    struct client_state_t* result = NULL;
    HASH_FIND(hh_name, server_state->client_states_user_ids, user_id, strlen(user_id), result);
    return result;
}

struct client_state_t* client_state_find_id(struct server_state_t* server_state, uint16_t client_id)
{
    struct client_state_t* result = NULL;
    HASH_FIND(hh_id, server_state->client_states_ids, &client_id, sizeof(client_id), result);
    return result;
}

void client_state_release_name(struct server_state_t* server_state, struct client_state_t* client_state)
{
    if (server_state->client_states_user_ids)
        HASH_DELETE(hh_name, server_state->client_states_user_ids, client_state);
}

void client_state_release_id(struct server_state_t* server_state, struct client_state_t* client_state)
{
    if (server_state->client_states_ids)
        HASH_DELETE(hh_id, server_state->client_states_ids, client_state);
}

void client_state_update(struct server_state_t* server_state, struct client_state_t* client_state)
{
    if (!client_state->inited)
        return;

    server_state_client_check_watch(client_state, server_state);
    server_python_update_player(&server_state->server_python, client_state);

    long now = server_time();

    if (now - client_state->sync_check_timer > 25)
    {
        client_state_sync(server_state, client_state);
    }

    if (client_state->object_sync_time && (now > client_state->object_sync_time))
    {
        uint8_t object_count = HASH_COUNT(client_state->object_sync_queue);

        ProtoStackObjectProperty props[object_count];
        ProtoStackObjectProperty* prev = NULL;

        int idx = 0;
        struct client_object_sync_queue_t* el;
        struct client_object_sync_queue_t* tmp;
        HASH_ITER(hh, client_state->object_sync_queue, el, tmp)
        {
            // server_printf("Syncing object %d predictions\n", el->ref->object.object_id);

            ProtoStackObjectProperty* prop = &props[idx];

            char* buff = malloc(sizeof(struct MSG_MOVE_OBJ_t));
            prop->key = '_';
            prop->value = buff;
            prop->value_size = sizeof(struct MSG_MOVE_OBJ_t);
            prop->prev = prev;

            struct MSG_MOVE_OBJ_t* msg = (struct MSG_MOVE_OBJ_t*)buff;
            msg->slot = el->slot;
            memcpy(msg->predictions, el->predictions, sizeof(msg->predictions));

            prev = prop;
            idx++;
        }

        uint8_t command = MSG_MOVE_OBJ;
        declare_arg_property_on_stack(id, OBJ_PROPERTY_ID, command, prev);
        client_state_send_proto_one_object(get_server_state(), client_state, &id);

        for (int i = 0; i < object_count; i++)
        {
            free((void*)props[i].value);
        }

        client_state->object_sync_time = 0;

        //client_printf(client_state, "Synced %d objects\n", object_count);

        HASH_ITER(hh, client_state->object_sync_queue, el, tmp)
        {
            HASH_DELETE(hh, client_state->object_sync_queue, el);
            free(el);
        }
    }
}

void server_notify_block_update(struct server_state_t* server_state, struct server_map_t* map, uint16_t x, uint16_t y)
{
    uint16_t chunk_x = x / MAP_CHUNK_SIZE;
    uint16_t chunk_y = y / MAP_CHUNK_SIZE;

    block_t old_block = map_get_block(&map->map, x, y);
    block_t block = server_python_map_refresh_block_code(&map->map, x, y, 1);

    if (old_block == block)
    {
        // code hasn't changed, nothing to tell about
        return;
    }

    struct client_state_t* send_to;
    LL_FOREACH(server_state->client_states, send_to)
    {
        struct server_map_chunk_subscriber_t* sub =
                server_map_chunk_get_subscriber(map, chunk_x, chunk_y, send_to);

        if (!sub)
            continue;

        sub->dirty = 1;

        union
        {
            uint16_t xy;
            struct
            {
                uint8_t x;
                uint8_t y;
            };
        } value;

        value.x = x % MAP_CHUNK_SIZE;
        value.y = y % MAP_CHUNK_SIZE;

        struct server_map_chunk_subscriber_outgoing_block_t* exists = NULL;

        HASH_FIND(hh, sub->modified_blocks, &value.xy, sizeof(uint16_t), exists);

        if (exists)
        {
            exists->code = block;
        }
        else
        {
            struct server_map_chunk_subscriber_outgoing_block_t* modified =
                    calloc(1, sizeof(struct server_map_chunk_subscriber_outgoing_block_t));

            modified->x = value.x;
            modified->y = value.y;
            modified->code = block;

            HASH_ADD(hh, sub->modified_blocks, xy, sizeof(uint16_t), modified);

            sub->modified_blocks_count++;
        }
    }
}

static void random_hex(char* str, int len)
{
    const char charset[] = "0123456789abcdef";
    for (int i = 0; i < len; ++i) {
        str[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    str[len] = '\0'; // Null-terminate the string
}

extern struct computer_t* server_state_computer_new(struct server_state_t* server_state, int namespace_id, const char* hash)
{
    pthread_mutex_lock(&server_state->computers_mutex);
    struct computer_t* c = malloc(sizeof(struct computer_t));
    computer_init(c, namespace_id, server_state);
    HASH_ADD_INT(server_state->computers, device.device_id, c);

    if (hash == NULL)
    {
        char hex[65];
        random_hex(hex, 64);
        c->hash = strdup(hex);
    }
    else
    {
        c->hash = strdup(hash);
    }

    unsigned _uthash_hastr_keylen = (unsigned)uthash_strlen(c->hash);
    HASH_ADD(hash_hh, server_state->computers_hashes, hash[0], _uthash_hastr_keylen, c);

    server_printf("New computer %d hash %s\n", c->device.device_id, c->hash);
    pthread_mutex_unlock(&server_state->computers_mutex);
    return c;
}

extern struct computer_t* server_state_computer_find_hash(struct server_state_t* server_state, const char* hash)
{
    pthread_mutex_lock(&server_state->computers_mutex);

    struct computer_t* out = NULL;
    unsigned _uthash_hfstr_keylen = (unsigned)uthash_strlen(hash);
    HASH_FIND(hash_hh, server_state->computers_hashes, hash, _uthash_hfstr_keylen, out);
    pthread_mutex_unlock(&server_state->computers_mutex);

    return out;
}

const char* device_session_data(struct server_main_thread_runnable_args* args)
{
    void* user = args->read.user;
    struct server_network_device_session_t* session = (struct server_network_device_session_t*)user;
    if (args->read.data_length == 0)
    {
        server_printf("device %s session %d closed\n", session->device->device.hostname, session->session_id);
        network_remove_read_device(&session->device->server_state->network_bindings, session->socket_fd);
        server_state_device_session_free(session->device->server_state, session->device, session);
    }
    else
    {
        server_printf("data on device %s session %d (%d bytes)\n", session->device->device.hostname,
                      session->session_id, (int)args->read.data_length);
        server_python_device_session_api_on_data(session->session, args->read.data, args->read.data_length);
        free(args->read.data);
    }
    return NULL;
}

static const char* device_accepted_device(struct server_main_thread_runnable_args* args)
{
    void* user = args->accept.user;
    struct server_network_device_t* device = (struct server_network_device_t*)user;

    PyObject* handler = server_python_device_get_session_handler(device);
    if (handler == NULL)
    {
        return NULL;
    }

    struct server_network_device_session_t* session = server_state_device_session_new(
        device->server_state, device, handler);

    session->socket_fd = args->accept.client_socket;
    server_printf("device %s accepted new session %d\n", device->device.hostname, session->session_id);

    network_add_read_device(&device->server_state->network_bindings, session->socket_fd,
        device_session_data, session);

    return NULL;
}

extern struct server_network_device_t* server_state_device_new(struct server_state_t* server_state,
    int namespace_id, const char* prefix)
{
    pthread_mutex_lock(&server_state->computers_mutex);
    struct server_network_device_t* c = calloc(1, sizeof(struct server_network_device_t));
    network_device_init(&c->device, namespace_id, DEVICE_OTHER, &server_state->network_bindings);
    c->accept_fd = -1;
    HASH_ADD_INT(server_state->devices, device.device_id, c);
    char hostname[128];
    snprintf(hostname, 128, "%s%d", prefix, c->device.device_id);
    network_device_assign_hostname(&c->device, hostname);
    pthread_mutex_unlock(&server_state->computers_mutex);
    return c;
}

void server_state_device_listen(struct server_network_device_t* device)
{
    pthread_mutex_lock(&device->server_state->computers_mutex);

    device->accept_fd = network_device_listen(&device->device);
    if (device->accept_fd < 0)
    {
        goto done;
    }

    network_add_accept_device(&device->server_state->network_bindings, device->accept_fd,
        device_accepted_device, device);

done:
    pthread_mutex_unlock(&device->server_state->computers_mutex);
}

void server_state_device_listen_close(struct server_network_device_t* device)
{
    if (device->accept_fd >= 0)
    {
        network_remove_accept_device(&device->server_state->network_bindings, device->accept_fd);
        close(device->accept_fd);
        device->accept_fd = -1;
    }
}

extern void server_state_device_free(struct server_state_t* server_state, struct server_network_device_t* device)
{
    pthread_mutex_lock(&server_state->computers_mutex);
    server_state_device_listen_close(device);
    HASH_DEL(server_state->devices, device);
    free(device);
    pthread_mutex_unlock(&server_state->computers_mutex);
}

struct server_network_device_session_t* server_state_device_session_new(struct server_state_t* server_state,
    struct server_network_device_t* device, PyObject* handler)
{
    static int session_id = 0;
    pthread_mutex_lock(&server_state->computers_mutex);
    struct server_network_device_session_t* c = calloc(1, sizeof(struct server_network_device_session_t));
    c->session_id = session_id++;
    c->device = device;
    c->session = server_python_device_api_allocate_session(&server_state->server_python, c, device, handler);
    DL_APPEND(device->sessions, c);
    pthread_mutex_unlock(&server_state->computers_mutex);
    return c;
}

extern void w5100_socket_unix_gen_path(uint16_t device_id,
    uint16_t namespace_id, uint16_t port, struct sockaddr_un* un);

struct server_network_device_session_t* network_device_connect_to(
    struct server_network_device_t* dev, struct network_device_t* to, int port,
    PyObject* handler)
{
    if (dev->device.namespace_id != to->namespace_id)
        return NULL;

    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0)
        return NULL;

    struct sockaddr_un un;
    w5100_socket_unix_gen_path(to->device_id, to->namespace_id, port, &un);

    if (connect(sock, (struct sockaddr*)&un, sizeof(un)) == -1) {
        server_printf(
            "failed to connect to %s by device %d socket %d; errno %d: %s\n",
            un.sun_path, dev->device.device_id, sock, compat_socket_get_error(),
            compat_socket_get_strerror() );
        close(sock);
        return NULL;
    }

    struct server_network_device_session_t* session =
            server_state_device_session_new(dev->server_state, dev, handler);

    session->socket_fd = sock;

    server_printf("device %s connected to %s on new session %d\n",
                  dev->device.hostname, to->hostname, session->session_id);

    network_add_read_device(&dev->server_state->network_bindings,
                            session->socket_fd, device_session_data, session);

    return session;
}


extern void server_state_device_session_free(struct server_state_t* server_state,
    struct server_network_device_t* device, struct server_network_device_session_t* session)
{
    network_remove_read_device(&device->server_state->network_bindings, session->socket_fd);
    server_python_device_session_api_free(session->session);
    DL_DELETE(device->sessions, session);
    free(session);
}

extern struct computer_t* server_state_computer_get(struct server_state_t* server_state, int computer_id)
{
    pthread_mutex_lock(&server_state->computers_mutex);
    struct computer_t* result;
    HASH_FIND_INT(server_state->computers, &computer_id, result);
    pthread_mutex_unlock(&server_state->computers_mutex);
    return result;
}

struct computer_t* server_state_computer_find(struct server_state_t* server_state, const char* hostname)
{
    struct network_device_t* device = network_device_find(&server_state->network_bindings, hostname);
    if (device == NULL)
        return NULL;

    if (device->device_type != DEVICE_COMPUTER)
        return NULL;

    return (struct computer_t*)device;
}

void server_state_computer_set_hostname(struct computer_t *computer, const char *hostname)
{
    network_device_assign_hostname(&computer->device, hostname);
}

extern uint8_t* server_state_computer_serialize(struct computer_t *computer, ssize_t* size)
{
    struct computer_snapshot_t* snapshot = calloc(1, sizeof(struct computer_snapshot_t));
    computer_serialize(computer, snapshot);
    *size = sizeof(struct computer_snapshot_t);
    return (uint8_t*)snapshot;
}

extern void server_state_computer_deserialize(struct computer_t *computer, uint8_t* data, ssize_t size)
{
    if (size != sizeof(struct computer_snapshot_t))
        return;

    struct computer_snapshot_t* snapshot = (struct computer_snapshot_t*)data;
    computer_deserialize(computer, snapshot);
}

void server_state_computer_free(struct server_state_t* server_state, struct computer_t* computer)
{
    pthread_mutex_lock(&server_state->computers_mutex);
    free(computer->hash);
    Py_DecRef(computer->computer_api);
    computer->computer_api = NULL;
    HASH_DEL(server_state->computers, computer);
    HASH_DELETE(hash_hh, server_state->computers_hashes, computer);
    free(computer);
    pthread_mutex_unlock(&server_state->computers_mutex);
}

uint16_t server_state_client_active_object(struct client_state_t* state)
{
    if (state->control_object)
    {
        return state->control_object;
    }

    return state->client_object;
}

static void server_state_force_watch(struct client_state_t* state, uint16_t object_id)
{
    struct server_state_t* server_state = state->state;

    struct server_object_reference_t* ref = server_map_get_object(&server_state->map, object_id);
    if (ref == NULL)
        return;

    {
        uint16_t x = OBJECT_PHY_TO_LOGICAL(ref->object.location.x);
        uint16_t y = OBJECT_PHY_TO_LOGICAL(ref->object.location.y);

        if (x > 16)
        {
            x -= 16;
        }
        else
        {
            x = 0;
        }

        if (y > 8)
        {
            y -= 8;
        }
        else
        {
            y = 0;
        }

        uint8_t _x = x / MAP_CHUNK_SIZE;
        uint8_t _y = y / MAP_CHUNK_SIZE;

        server_state_client_set_watch(state, state->state, _x, _y);
    }
}

static void server_state_object_set_client_id(struct client_state_t* state, uint16_t object_id, uint16_t client_id)
{
    struct server_state_t* server_state = state->state;

    struct server_object_reference_t* ref = server_map_get_object(&server_state->map, object_id);
    if (ref == NULL)
        return;

    uint8_t synced_slot = server_state_client_find_synced_object(state, server_state, &ref->object);
    // if object is not being synced then do nothing
    if (synced_slot == 0xFF)
    {
        client_printf(state, "cannot set client_id for object %d to %d\n", object_id, client_id);

        return;
    }

    client_printf(state, "setting client_id for slot %d to %d\n", synced_slot, client_id);

    declare_arg_property_on_stack(slot_id, 's', synced_slot, NULL);
    declare_arg_property_on_stack(_client_id, 'i', client_id, &slot_id);
    uint8_t command = MSG_OBJ_SET_CLIENT_ID;
    declare_arg_property_on_stack(id, OBJ_PROPERTY_ID, command, &_client_id);
    client_state_send_proto_one_object(server_state, state, &id);
}

void server_state_client_set_active_object(struct client_state_t* state, uint16_t object_id)
{
    struct server_state_t* server_state = state->state;

    if (object_id)
    {
        state->control_object = object_id;

        struct server_object_reference_t* ref = server_map_get_object(&server_state->map, object_id);
        if (ref == NULL)
            return;

        // Only one map object may carry this client's id; otherwise sync skips and
        // client-side MSG_SYNC_OBJ both attach as my_player_object ("controlling both").
        if (state->client_object && state->client_object != object_id)
        {
            struct server_object_reference_t* pref = server_map_get_object(&server_state->map, state->client_object);
            if (pref)
            {
                pref->object.client_id = 0;
                pref->client_id = 0;
                pref->force_sync = 1;
                set_object_dirty(&pref->object);

                server_state_object_set_client_id(state, state->client_object, 0);
            }
        }

        ref->object.client_id = state->client_id;
        ref->client_id = state->client_id;
        ref->force_sync = 1;
        set_object_dirty(&ref->object);

        // need to force-sync that object
        server_state_object_set_client_id(state, object_id, state->client_id);
    }
    else
    {
        struct server_object_reference_t* ref = server_map_get_object(&server_state->map, state->control_object);
        if (ref)
        {
            server_state_object_set_client_id(state, state->control_object, 0);

            ref->object.client_id = 0;
            ref->client_id = 0;
        }

        state->control_object = 0;

        if (state->client_object)
        {
            struct server_object_reference_t* pref = server_map_get_object(&server_state->map, state->client_object);
            if (pref)
            {
                pref->object.client_id = state->client_id;
                pref->client_id = state->client_id;
                pref->force_sync = 1;
                set_object_dirty(&pref->object);
            }

            // need to force-sync the client's object (if any)
            server_state_object_set_client_id(state, state->client_object, state->client_id);
        }
    }
}
