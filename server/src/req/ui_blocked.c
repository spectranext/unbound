#include "req_handlers.h"
#include "proto_objects.h"
#include "server.h"
#include "server_python.h"

const char* req_handle_ui_blocked_main_thread(struct server_main_thread_runnable_args* args)
{
    server_printf("ui: client_id=%d user=%s blocked=%d\n",
        args->state->client_id, args->state->user_name, args->ui_blocked.blocked);

    if (args->ui_blocked.blocked)
    {
        server_python_block_notifications(&args->state->state->server_python, args->state, "ui");
    }
    else
    {
        server_python_unblock_notifications(&args->state->state->server_python, args->state, "ui");
    }

    return NULL;
}

const char* req_handle_ui_blocked(struct client_state_t* state, struct request_handler_response_chain_t** response)
{
    struct server_main_thread_runnable_args post_args;
    post_args.state = state;
    post_args.response = response;
    post_args.ui_blocked.blocked = get_uint8_property(state->receiving_objects[0], 'b', 0);

    return server_state_post_runnable_wait(state->state, req_handle_ui_blocked_main_thread, post_args, &state->post_wait);
}
