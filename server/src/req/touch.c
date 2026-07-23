#include "req_handlers.h"
#include "proto_objects.h"
#include "server.h"
#include "server_map.h"
#include "version.h"
#include <stdio.h>
#include <math.h>

const char* req_handle_touch_main_thread(struct server_main_thread_runnable_args* args)
{
    struct server_map_t* map = &args->state->state->map;
    if (map == NULL)
    {
        return NULL;
    }

    uint16_t touch_x = args->touch.x;
    uint16_t touch_y = args->touch.y;

    struct server_object_reference_t* ref = server_map_get_object(map, server_state_client_active_object(args->state));
    if (ref == NULL)
        return NULL;

    if (abs((int32_t)touch_x - OBJECT_PHY_TO_LOGICAL((int32_t)ref->object.location.x)) > 5)
        return NULL;

    if (abs((int32_t)touch_y - OBJECT_PHY_TO_LOGICAL((int32_t)ref->object.location.y)) > 5)
        return NULL;

    if (map_get_object_state(ref) == OBJECT_STATE_CONTROL)
    {
        return "Object's busy";
    }

    uint16_t scheduled_time = 0;
    server_python_player_touch(&args->state->state->server_python, args->state, touch_x, touch_y, &scheduled_time);

    {
        uint8_t p = scheduled_time ? 1 : 0;
        declare_arg_property_on_stack(_t, 'p', p, NULL);
        server_request_add_response(args->response, proto_object_allocate(&_t));
    }

    return NULL;
}

const char* req_handle_touch(struct client_state_t* state, struct request_handler_response_chain_t** response)
{
    if (!state->inited)
    {
        return "Incorrect state";
    }

    uint16_t x = get_uint16_property(state->receiving_objects[0], 'x', 0);
    uint16_t y = get_uint16_property(state->receiving_objects[0], 'y', 0);

    struct server_main_thread_runnable_args post_args;

    post_args.state = state;
    post_args.response = response;
    post_args.touch.x = x;
    post_args.touch.y = y;

    return server_state_post_runnable_wait(state->state, req_handle_touch_main_thread, post_args, &state->post_wait);
}