#include "req_handlers.h"
#include "proto_objects.h"
#include "server.h"
#include "server_map.h"
#include "version.h"
#include <stdio.h>
#include <math.h>

const char* req_handle_touch_cancel_main_thread(struct server_main_thread_runnable_args* args)
{
    client_printf(args->state, "Cancelling touch\n");

    struct server_map_t* map = &args->state->state->map;
    if (map == NULL)
    {
        return NULL;
    }

    struct server_object_reference_t* ref = server_map_get_object(map, server_state_client_active_object(args->state));
    if (ref == NULL)
        return NULL;

    if (server_python_player_touch_cancel(&args->state->state->server_python, args->state))
    {
        client_printf(args->state, "Scheduled touch was cancelled\n");
    }

    return NULL;
}

const char* req_handle_touch_cancel(struct client_state_t* state, struct request_handler_response_chain_t** response)
{
    struct server_main_thread_runnable_args post_args;

    post_args.state = state;
    post_args.response = response;

    return server_state_post_runnable_wait(state->state, req_handle_touch_cancel_main_thread, post_args, &state->post_wait);
}