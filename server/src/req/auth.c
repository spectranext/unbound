#include "req_handlers.h"
#include "proto_objects.h"
#include "server.h"
#include "server_map.h"
#include "utils.h"
#include <stdio.h>

static __thread char error_reason[64];

const char* req_handle_auth_main_thread(struct server_main_thread_runnable_args* args)
{
    struct client_auth_result_t auth_result = {};

    if (args->auth.token[0])
    {
        client_printf(args->state, "presented token %s\n", args->auth.token);
    }

    if (server_python_client_auth(&args->state->state->server_python, args->auth.token, &auth_result))
    {
        if (strlen(auth_result.error))
        {
            client_printf(args->state, "Failed to auth: %s\n", auth_result.error);
            strcpy(error_reason, auth_result.error);
            return error_reason;
        }
        else
        {
            client_printf(args->state, "Failed to initiate auth\n");
            return "Failed to initiate auth";
        }
    }

    if (client_state_find_user_id(args->state->state, auth_result.user_id))
    {
        client_printf(args->state, "This user already connected\n");
        return "This user already connected";
    }

    client_state_assign_user_id(args->state->state, args->state, auth_result.user_id, auth_result.user_name);

    uint16_t new_client_id = args->state->state->next_client_id++;
    client_printf(args->state, "fresh new client %d\n", new_client_id);
    client_state_assign_client_id(args->state->state, args->state, new_client_id);

    if (server_python_allocate_client(&args->state->state->server_python, args->state->client_id))
    {
        client_printf(args->state, "could not allocate client!\n");
        return "could not allocate client!";
    }

    server_python_assign_py_player_callbacks(&args->state->state->server_python, args->state);

    server_python_on_new_client(
        &args->state->state->server_python, args->state->client_id, args->state->state->scenario);

    client_printf(args->state, "new_client\n");

    server_state_client_sync_stats(args->state, args->state->state);
    args->state->inited = 1;

    char token_output[68] = {};
    char* token_result = NULL;

    if (strlen(auth_result.token) && (strcmp(auth_result.token, args->auth.token) != 0))
    {
        strcpy(token_output, auth_result.token);
        token_result = token_output;

        client_printf(args->state, "was given token %s\n", token_output);
    }

    declare_arg_property_on_stack(map_width, 'w', args->state->state->map.map.width, NULL);
    declare_arg_property_on_stack(map_height, 'h', args->state->state->map.map.height, &map_width);
    declare_arg_property_on_stack(your_id, 'i', args->state->client_id, &map_height);
    declare_str_property_on_stack(_t, 't', token_result ? token_result : "", &your_id);

    server_request_add_response(args->response, proto_object_allocate(&_t));

    {
        struct server_data_entry_t* data_entry;
        LL_FOREACH(args->state->state->server_data.data_entries, data_entry)
        {
            uint16_t payload_len = data_entry->payload_len;
            uint16_t max_chunk_size = 1600;

            if (payload_len > max_chunk_size)
            {
                uint8_t* ptr = data_entry->payload;
                uint16_t limit = max_chunk_size;
                uint16_t offset = 0;
                uint16_t remaining = payload_len - limit;

                {
                    declare_arg_property_on_stack(sz, 's', payload_len, NULL);
                    declare_variable_property_on_stack(data, 'p', ptr, limit, &sz);
                    server_request_add_response(args->response, proto_object_allocate(&data));
                }

                ptr += limit;
                offset += limit;

                while (remaining > 0)
                {
                    limit = remaining > max_chunk_size ? max_chunk_size : remaining;

                    declare_arg_property_on_stack(_offset, 'o', offset, NULL);
                    declare_variable_property_on_stack(data, 'p', ptr, limit, &_offset);
                    server_request_add_response(args->response, proto_object_allocate(&data));

                    ptr += limit;
                    offset += limit;
                    remaining -= limit;
                }
            }
            else
            {
                declare_arg_property_on_stack(sz, 's', payload_len, NULL);
                declare_variable_property_on_stack(data, 'p', data_entry->payload, payload_len, &sz);
                server_request_add_response(args->response, proto_object_allocate(&data));
            }

        }
    }

    {
        char msg[200] = {};
        sprintf(msg, "Player %s connected.", args->state->user_name);

        struct client_state_t* client_state;
        LL_FOREACH(args->state->state->client_states, client_state)
        {
            if (client_state != args->state)
            {
                client_state_notify_message(args->state->state, client_state, msg, NOTIFY_MESSAGE_COLOR_BRIGHT);
            }
        }
    }

    server_python_set_client_authenticated(&args->state->state->server_python, args->state);

    return NULL;
}

const char* req_handle_auth(struct client_state_t* state, struct request_handler_response_chain_t** response)
{
    if (state->inited)
    {
        return "Incorrect state";
    }

    char token[68] = {};

    get_str_property(state->receiving_objects[0], 't', token, sizeof(token));

    struct server_main_thread_runnable_args post_args;
    post_args.state = state;
    post_args.response = response;

    strcpy(post_args.auth.token, token);
    post_args.auth.request_id = state->proto.request_id;

    return server_state_post_runnable_wait(state->state, req_handle_auth_main_thread, post_args, &state->post_wait);
}