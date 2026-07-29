#ifndef __SERVER_REQ_HANDLERS_H
#define __SERVER_REQ_HANDLERS_H

#include <proto_objects.h>
#include "uthash.h"
#include "utlist.h"

struct client_state_t;
struct server_state_t;
struct proto_req_processor_t;

struct request_handler_response_chain_t
{
    ProtoObject* response;
    struct request_handler_response_chain_t* next;
};

extern void server_request_add_response(struct request_handler_response_chain_t** response, ProtoObject* object);

typedef const char* (*server_request_handler_cb)(struct client_state_t* state, struct request_handler_response_chain_t** response);

struct request_handler_t {
    const char* name;
    server_request_handler_cb cb;
    UT_hash_handle hh;
};

extern void register_server_handlers(struct server_state_t* state);
extern void register_client_handlers(struct proto_req_processor_t* handle, struct client_state_t* client_state);

extern const char* req_handle_query(struct client_state_t* state, struct request_handler_response_chain_t** response);
extern const char* req_handle_query_option(struct client_state_t* state, struct request_handler_response_chain_t** response);
extern const char* req_handle_auth(struct client_state_t* state, struct request_handler_response_chain_t** response);
extern const char* req_handle_move(struct client_state_t* state, struct request_handler_response_chain_t** response);
extern const char* req_handle_touch(struct client_state_t* state, struct request_handler_response_chain_t** response);
extern const char* req_handle_touch_cancel(struct client_state_t* state, struct request_handler_response_chain_t** response);
extern const char* req_handle_chat(struct client_state_t* state, struct request_handler_response_chain_t** response);
extern const char* req_handle_terminal(struct client_state_t* state, struct request_handler_response_chain_t** response);
extern const char* req_handle_hit(struct client_state_t* state, struct request_handler_response_chain_t** response);
extern const char* req_handle_aim(struct client_state_t* state, struct request_handler_response_chain_t** response);
extern const char* req_handle_action(struct client_state_t* state, struct request_handler_response_chain_t** response);
extern const char* req_handle_download(struct client_state_t* state, struct request_handler_response_chain_t** response);
extern const char* req_handle_client_binary(struct client_state_t* state, struct request_handler_response_chain_t** response);
extern const char* req_handle_ui_blocked(struct client_state_t* state, struct request_handler_response_chain_t** response);

#endif
