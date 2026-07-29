#ifndef __SERVER_DATA_H__
#define __SERVER_DATA_H__

#include <stdint.h>
#include <ut/utlist.h>
#include <ut/uthash.h>
#include "Python.h"

#define SERVER_DATA_MODULE_NAMESPACE_COUNT 4
#define SERVER_DATA_MODULE_NAME_SIZE 32

enum server_data_kv_type_t
{
    SERVER_KV_UNKNOWN = 0,
    SERVER_KV_INT,
    SERVER_KV_STRING,
};

struct server_data_kv_t
{
    char name[32];
    enum server_data_kv_type_t type;
    union {
        uint8_t as_int;
        const char* as_string;
    };
    UT_hash_handle hh;
};

struct server_data_entry_t
{
    char name[32];
    uint8_t index;
    uint8_t* payload;
    uint16_t payload_len;
    struct server_data_kv_t* extra;
    struct server_data_entry_t* next;
    struct server_data_entry_t* prev;
    UT_hash_handle hh;
};

struct client_state_t;
struct server_state_t;

struct server_screen_t
{
    char* name;
    uint8_t data[6144 + 768];
    UT_hash_handle hh;
};

struct server_data_t
{
    uint8_t entries_count;
    struct server_data_entry_t* data_entries;
    struct server_data_entry_t* keys;
};

extern void server_screens_init(struct server_state_t* server_state);
extern uint8_t server_data_init(struct server_data_t* server_data, const char* filename);
extern uint8_t server_data_post_process(struct server_data_t* server_data);
extern struct server_data_entry_t* find_data_entry(struct server_data_t* server_data, const char* name);
extern struct server_data_kv_t* get_data_entry_prop(struct server_data_entry_t* e, const char* key);
extern uint8_t get_data_entry_prop_int(struct server_data_entry_t* e, const char* key, uint8_t def);
extern const char* get_data_entry_prop_str(struct server_data_entry_t* e, const char* key);
extern void server_data_push_module(struct client_state_t* client_state, const char* name);
extern void server_data_module_action(struct client_state_t* client_state, const char* name, PyObject* action);

#endif
