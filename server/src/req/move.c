#include "req_handlers.h"
#include "proto_objects.h"
#include "server.h"
#include "server_map.h"
#include "version.h"
#include <stdio.h>
#include <math.h>

#define MIN(a,b) ((a) < (b) ? (a) : (b))

const char* req_handle_move_main_thread(struct server_main_thread_runnable_args* args)
{
    struct server_map_t* map = &args->state->state->map;
    if (map == NULL)
    {
        return NULL;
    }

    struct server_object_reference_t* ref = server_map_get_object(map, server_state_client_active_object(args->state));
    if (ref == NULL)
    {
        return NULL;
    }

    struct map_object_t* o = &ref->object;

    o->target.x = args->move.x;
    o->target.y = args->move.y;

    if (abs(o->target.x - o->location.x) > OBJECT_TARGET_FORCE_SYNC)
    {
        o->target.x = o->location.x;
        ref->force_sync = 1;
    }

    if (abs(o->target.y - o->location.y) > OBJECT_TARGET_FORCE_SYNC)
    {
        o->target.y = o->location.y;
        ref->force_sync = 1;
    }

    o->speed.x = args->move.speed_x;
    o->speed.y = args->move.speed_y;
    ref->adjust_target = 1;

    if (o->speed.x < 0)
    {
        SET_F0(o, OBJECT_F0_LOOKING_LEFT);
    }
    else if (o->speed.x > 0)
    {
        RESET_F0(o, OBJECT_F0_LOOKING_LEFT);
    }

    // we need to inform everyone
    SET_F0(o, OBJECT_F0_DIRTY);

    return NULL;
}

const char* req_handle_move(struct client_state_t* state, struct request_handler_response_chain_t** response)
{
    if (!state->inited)
    {
        return "Incorrect state";
    }

    uint16_t x = get_uint16_property(state->receiving_objects[0], 'x', 0);
    uint16_t y = get_uint16_property(state->receiving_objects[0], 'y', 0);

    int8_t speed_x = (int8_t)get_uint8_property(state->receiving_objects[0], 'X', 0);
    int8_t speed_y = (int8_t)get_uint8_property(state->receiving_objects[0], 'Y', 0);

    struct server_main_thread_runnable_args post_args;

    post_args.state = state;
    post_args.response = response;
    post_args.move.x = x;
    post_args.move.y = y;
    post_args.move.speed_x = speed_x;
    post_args.move.speed_y = speed_y;

    return server_state_post_runnable_wait(state->state, req_handle_move_main_thread, post_args, &state->post_wait);
}
