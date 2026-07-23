#include "req_handlers.h"
#include "proto_objects.h"
#include "server.h"

const char* req_handle_client_binary(struct client_state_t* state, struct request_handler_response_chain_t** response)
{
    declare_arg_property_on_stack(_addr, 'a', state->state->client_binary_addr, NULL);
    declare_arg_property_on_stack(_size, 's', state->state->client_binary_size, &_addr);
    server_request_add_response(response, proto_object_allocate(&_size));

    return NULL;
}