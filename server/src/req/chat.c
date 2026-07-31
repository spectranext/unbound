#include "req_handlers.h"
#include "proto_objects.h"
#include "server.h"
#include "server_map.h"
#include "server_python.h"
#include "version.h"
#include "messages.h"
#include <stdio.h>
#include <math.h>

const char* req_handle_chat_main_thread(struct server_main_thread_runnable_args* args)
{
    server_python_map_on_chat_message(&args->state->state->server_python, args->state,
        args->chat.message);
    return NULL;
}

const char* req_handle_chat(struct client_state_t* state, struct request_handler_response_chain_t** response)
{
    ProtoObjectProperty* message = find_property(state->receiving_objects[0], 'm');

    if (message == NULL)
    {
        return "No message";
    }

    char* msg = copy_str_property(message);
    struct server_main_thread_runnable_args post_args = {0};
    post_args.state = state;
    post_args.response = response;
    strncpy(post_args.chat.message, msg, sizeof(post_args.chat.message) - 1);
    server_printf("chat: request from client_id=%d user=%s message=%s\n",
        state->client_id, state->user_name, post_args.chat.message);
    free(msg);
    return server_state_post_runnable_wait(state->state, req_handle_chat_main_thread, post_args, &state->post_wait);
}
