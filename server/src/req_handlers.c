#include "req_handlers.h"
#include "proto.h"
#include "proto_objects.h"
#include "proto_req.h"
#include "server.h"
#include "server_map.h"
#include "messages.h"
#include <stdio.h>

static void server_handle_request(struct server_state_t* state,
                                  const char* name, server_request_handler_cb handler)
{
    struct request_handler_t* new_handler = calloc(1, sizeof(struct request_handler_t));
    new_handler->name = name;
    new_handler->cb = handler;
    HASH_ADD_STR(state->handlers, name, new_handler);
}

void register_server_handlers(struct server_state_t* state)
{
    state->handlers = NULL;
    server_handle_request(state, MSG_QUERY_OPTION, req_handle_query_option);
    server_handle_request(state, MSG_QUERY, req_handle_query);
    server_handle_request(state, MSG_AUTH, req_handle_auth);
    server_handle_request(state, MSG_MOVE, req_handle_move);
    server_handle_request(state, MSG_TOUCH, req_handle_touch);
    server_handle_request(state, MSG_TOUCH_CANCEL, req_handle_touch_cancel);
    server_handle_request(state, MSG_CLIENT_CHAT, req_handle_chat);
    server_handle_request(state, MSG_TERMINAL, req_handle_terminal);
    server_handle_request(state, MSG_HIT, req_handle_hit);
    server_handle_request(state, MSG_AIM, req_handle_aim);
    server_handle_request(state, MSG_ACTION, req_handle_action);
    server_handle_request(state, MSG_DOWNLOAD, req_handle_download);
    server_handle_request(state, MSG_CLIENT_BINARY, req_handle_client_binary);
}

static void free_client_state_recv_objects(struct client_state_t* client_state)
{
    for (int i = 0; i < client_state->receiving_objects_num; i++)
    {
        free(client_state->receiving_objects[i]);
    }

    client_state->receiving_objects_num = 0;
}

static void client_request_new(int socket, struct proto_process_t* proto, void* user)
{
    struct client_state_t* client_state = (struct client_state_t*)user;
    free_client_state_recv_objects(client_state);
}

static void client_request_new_object(int socket, struct proto_process_t* proto, ProtoObject* object, void* user)
{
    struct client_state_t* client_state = (struct client_state_t*)user;

    if (client_state->receiving_objects_num >= MAX_RECEIVING_OBJECTS)
    {
        return;
    }

    client_state->receiving_objects[client_state->receiving_objects_num++] = proto_object_copy(object);
}

static const char* client_request_complete(int socket, struct proto_process_t* proto, void* user)
{
    struct client_state_t* client_state = (struct client_state_t*)user;

    if (client_state->receiving_objects_num == 0)
    {
        return "Empty request";
    }

    ProtoObject* first_object = client_state->receiving_objects[0];
    ProtoObjectProperty* id = find_property(first_object, OBJ_PROPERTY_ID);
    if (id == NULL)
    {
        return "Unknown req id";
    }

    char* request_id = copy_str_property(id);
    // client_printf(client_state, "request: %s\n", request_id);

    struct request_handler_t* handler = NULL;
    HASH_FIND_STR(client_state->state->handlers, request_id, handler);
    free(request_id);

    if (handler == NULL)
    {
        return "Unknown request";
    }

    struct request_handler_response_chain_t* response_head = NULL;

    const char* result = handler->cb(client_state, &response_head);
    if (result)
    {
        return result;
    }

    int response_count = 0;

    {
        struct request_handler_response_chain_t* elem;
        LL_COUNT(response_head, elem, response_count);
    }

    if (response_count)
    {
        ProtoObject** as_array = calloc(response_count, sizeof(ProtoObject*));

        {
            struct request_handler_response_chain_t* elem;
            int counter = 0;
            LL_FOREACH(response_head, elem)
            {
                as_array[counter++] = elem->response;
            }
        }

        proto_send(socket, as_array, response_count, proto->request_id, PROTO_FLAG_RESPONSE);

        {
            struct request_handler_response_chain_t* elem;
            struct request_handler_response_chain_t* tmp;

            LL_FOREACH_SAFE(response_head, elem, tmp)
            {
                LL_DELETE(response_head, elem);
                free(elem->response);
                free(elem);
            }
        }

        free(as_array);
    }

    return NULL;
}

void register_client_handlers(struct proto_req_processor_t* handle, struct client_state_t* client_state)
{
    proto_req_init_processor(handle, client_request_new, client_request_new_object, client_request_complete,
        client_state);
}

void server_request_add_response(struct request_handler_response_chain_t** response, ProtoObject* object)
{
    struct request_handler_response_chain_t* entry = calloc(1, sizeof(struct request_handler_response_chain_t));
    entry->response = object;
    LL_APPEND(*response, entry);
}