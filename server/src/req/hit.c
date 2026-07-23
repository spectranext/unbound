#include "req_handlers.h"
#include "proto_objects.h"
#include "server.h"
#include "server_map.h"

const char* req_handle_hit_main_thread(struct server_main_thread_runnable_args* args)
{
    struct server_map_t* map = &args->state->state->map;
    if (map == NULL)
    {
        return NULL;
    }

    struct server_object_reference_t* ref = server_map_get_object(map, server_state_client_active_object(args->state));
    if (ref == NULL)
        return NULL;

    if (map_get_object_state(ref) == OBJECT_STATE_CONTROL)
    {
        return "Object's busy";
    }

    server_python_player_hit(&args->state->state->server_python, args->state, args->hit.angle);
    return NULL;
}

const char* req_handle_hit(struct client_state_t* state, struct request_handler_response_chain_t** response)
{
    if (!state->inited)
    {
        return "Incorrect state";
    }

    struct server_main_thread_runnable_args post_args;

    post_args.state = state;
    post_args.response = response;
    post_args.hit.angle = get_uint16_property(state->receiving_objects[0], 'a', 0);

    server_state_post_runnable(state->state, req_handle_hit_main_thread, post_args);
    return NULL;
}