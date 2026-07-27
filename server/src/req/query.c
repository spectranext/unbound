#include "req_handlers.h"
#include "proto_objects.h"
#include "server.h"
#include "server_map.h"
#include "version.h"
#include <stdio.h>

const char* req_handle_query_main_thread(struct server_main_thread_runnable_args* args)
{
    client_printf(args->state, "query %s\n", args->query.query);

    if (server_python_player_query(&args->state->state->server_python, args->state,
        args->query.query))
    {
        return "Cannot process query";
    }

    return NULL;
}

const char* req_handle_query(struct client_state_t* state, struct request_handler_response_chain_t** response)
{
    if (!state->inited)
    {
        return "Incorrect state";
    }

    ProtoObjectProperty* query = find_property(state->receiving_objects[0], 'q');

    if (query == NULL)
    {
        return "No query";
    }

    char* q = copy_str_property(query);
    struct server_main_thread_runnable_args post_args;
    strncpy(post_args.query.query, q, sizeof(post_args.query.query));

    free(q);

    post_args.state = state;
    post_args.response = response;

    return server_state_post_runnable_wait(state->state, req_handle_query_main_thread, post_args, &state->post_wait);
}
