#include "req_handlers.h"
#include "proto_objects.h"
#include "server.h"
#include "server_map.h"
#include "version.h"
#include <stdio.h>

const char* req_handle_query_option_main_thread(struct server_main_thread_runnable_args* args)
{
    if (server_python_player_query_option(&args->state->state->server_python, args->state,
        args->query_option.option, args->query_option.action))
    {
        return "Cannot process option";
    }

    client_printf(args->state, "query option %d action %s\n",
        args->query_option.option, args->query_option.action);

    return NULL;
}

const char* req_handle_query_option(struct client_state_t* state, struct request_handler_response_chain_t** response)
{
    uint8_t option = get_uint8_property(state->receiving_objects[0], 'o', 0);
    ProtoObjectProperty* action = find_property(state->receiving_objects[0], 'a');

    struct server_main_thread_runnable_args post_args;
    post_args.query_option.option = option;

    if (action)
    {
        char* a = copy_str_property(action);

        strncpy(post_args.query_option.action, a, sizeof(post_args.query_option.action));

        free(a);
    }
    else
    {
        // close (cancel) action
        strcpy(post_args.query_option.action, "");
    }

    post_args.state = state;
    post_args.response = response;

    return server_state_post_runnable_wait(state->state, req_handle_query_option_main_thread, post_args, &state->post_wait);
}
