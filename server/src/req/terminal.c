#include "req_handlers.h"
#include "proto_objects.h"
#include "server.h"
#include <stdio.h>

const char* req_handle_terminal_main_thread(struct server_main_thread_runnable_args* args)
{
    struct client_state_t* state = args->state;
    server_python_terminal(&args->state->state->server_python, state, args->chat.message);
    return NULL;
}

const char* req_handle_terminal(struct client_state_t* state, struct request_handler_response_chain_t** response)
{
    ProtoObjectProperty* message = find_property(state->receiving_objects[0], 'm');

    if (message == NULL)
    {
        return "No message";
    }

    char* msg = copy_str_property(message);
    struct server_main_thread_runnable_args post_args;
    post_args.state = state;
    post_args.response = response;
    strncpy(post_args.chat.message, msg, sizeof(post_args.chat.message));
    free(msg);
    return server_state_post_runnable_wait(state->state, req_handle_terminal_main_thread, post_args, &state->post_wait);
}