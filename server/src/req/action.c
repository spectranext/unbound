#include "req_handlers.h"
#include "proto_objects.h"
#include "server.h"
#include <stdio.h>

const char* req_handle_action_main_thread(struct server_main_thread_runnable_args* args)
{
    struct client_state_t* state = args->state;
    server_python_action(
        &args->state->state->server_python, state,
        args->action.message,
        args->action.payload, args->action.payload_len);
    return NULL;
}

const char* req_handle_action(struct client_state_t* state, struct request_handler_response_chain_t** response)
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
    strncpy(post_args.action.message, msg, sizeof(post_args.action.message));

    ProtoObjectProperty* payload = find_property(state->receiving_objects[0], 'p');
    if (payload && payload->value_size < sizeof(post_args.action.payload))
    {
        post_args.action.payload_len = payload->value_size;
        memcpy(post_args.action.payload, payload->value, payload->value_size);
    }
    else
    {
        post_args.action.payload_len = 0;
    }

    free(msg);
    return server_state_post_runnable_wait(state->state, req_handle_action_main_thread, post_args, &state->post_wait);
}