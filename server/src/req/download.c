#include "req_handlers.h"
#include "proto_objects.h"
#include "server.h"

const char* req_handle_download(struct client_state_t* state, struct request_handler_response_chain_t** response)
{
    uint16_t addr = get_uint16_property(state->receiving_objects[0], 'a', state->state->client_binary_addr);
    uint16_t requested_size = get_uint16_property(state->receiving_objects[0], 's', state->state->client_binary_size);

    if (addr + requested_size > state->state->client_binary_addr + state->state->client_binary_size)
    {
        return "Incorrect request";
    }

    client_printf(state, "Requested clienty binary chunk %d of size %d\n", addr, requested_size);

    uint8_t* data = state->state->client_binary + addr - state->state->client_binary_addr;

    declare_arg_property_on_stack(_offset, 'a', addr, NULL);
    declare_variable_property_on_stack(_data, 'd', data, requested_size, &_offset);
    server_request_add_response(response, proto_object_allocate(&_data));

    return NULL;
}