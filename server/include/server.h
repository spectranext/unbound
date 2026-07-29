#ifndef __SERVER_H
#define __SERVER_H

#include "server_map.h"
#include "server_python.h"
#include "req_handlers.h"
#include "proto.h"
#include <pthread.h>
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include "utlist.h"
#include "server_data.h"
#include "network.h"

#define CLIENT_MAX_SYNCED_CHUNKS (128)
#define CLIENT_OBJECT_SYNC_BATCHING (80)

struct xfs_stat_info;

struct server_main_thread_runnable_args
{
    union
    {
        struct
        {
            struct client_state_t* state;
            struct request_handler_response_chain_t** response;
            union
            {
                struct
                {
                    char token[68];
                    uint16_t request_id;
                } auth;
                struct
                {
                    uint16_t x;
                    uint16_t y;
                    int8_t speed_x;
                    int8_t speed_y;
                } move;
                struct
                {
                    uint16_t x;
                    uint16_t y;
                } touch;
                struct
                {
                    char message[128];
                } chat;
                struct
                {
                    char query[128];
                } query;
                struct
                {
                    uint8_t option;
                    char action[128];
                } query_option;
                struct
                {
                    char message[128];
                    uint8_t payload[64];
                    uint8_t payload_len;
                } action;
                struct
                {
                    uint16_t angle;
                } hit;
                struct
                {
                    uint8_t blocked;
                } ui_blocked;
                struct
                {
                    uint16_t angle;
                } aim;
            };
        };

        struct
        {
            void* user;
            int client_socket;
        } accept;

        struct
        {
            void* user;
            uint8_t* data;
            long data_length;
        } read;

        struct
        {
            void* user;
        } port_write_bind;

        struct
        {
            void* user;
        } port_read_bind;

        struct
        {
            void* user;
        } memory_write_bind;

        struct
        {
            void* user;
        } memory_read_bind;

        struct
        {
            void* user;
            uint8_t value;
        } port_write;

        struct
        {
            void* user;
            uint8_t result;
            uint8_t* result_ptr;
        } port_read;

        struct
        {
            void* user;
            uint16_t offset;
            uint8_t value;
        } memory_write;

        struct
        {
            void* user;
            uint16_t offset;
            uint8_t result;
        } memory_read;

        struct
        {
            void* user;
            const char* path;
            int flags;
            uint8_t is_dir;
            void** handle;
            int16_t* result;
        } xfs_open;

        struct
        {
            void* user;
            void* handle;
            uint8_t* buffer;
            uint16_t size;
            int16_t* result;
        } xfs_read;

        struct
        {
            void* user;
            void* handle;
            const uint8_t* buffer;
            uint16_t size;
            int16_t* result;
        } xfs_write;

        struct
        {
            void* user;
            void* handle;
            int16_t* result;
        } xfs_close;

        struct
        {
            void* user;
            void* handle;
            uint8_t mode;
            uint32_t offset;
            int32_t* result;
        } xfs_seek;

        struct
        {
            void* user;
            void* handle;
            struct xfs_stat_info* info;
            int16_t* result;
        } xfs_readdir;

        struct
        {
            void* user;
        } xfs_path_bind;
    };
};

typedef const char* (*main_thread_runnable_cb)(struct server_main_thread_runnable_args* args);

struct server_main_thread_runnable_wait
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    uint8_t complete;
    const char* error;
};

struct server_main_thread_runnable
{
    main_thread_runnable_cb callback;
    struct server_main_thread_runnable_args args;
    struct server_main_thread_runnable_wait* wait;
    struct server_main_thread_runnable* next;
};

struct callback_method_schedule_t
{
    PyObject * callback;
    long time;
    struct callback_method_schedule_t* next;
};

struct block_method_schedule_t
{
    char* method;
    uint16_t x;
    uint16_t y;
    long time;
    struct block_method_schedule_t* next;
};

struct computer_rom_t
{
    uint8_t* rom;
    size_t size;
};

struct server_network_device_t;

struct server_network_device_session_t
{
    int                                         session_id;
    PyObject*                                   session;
    int                                         socket_fd;
    struct server_network_device_t*             device;

    struct server_network_device_session_t*     next;
    struct server_network_device_session_t*     prev;
};

struct server_network_device_t
{
    struct network_device_t                     device;
    struct server_state_t*                      server_state;
    struct server_network_device_session_t*     sessions;
    int                                         accept_fd;
    PyObject*                                   handler;
    PyObject*                                   py;
    UT_hash_handle                              hh;
};

struct server_bullet_t;
struct server_object_delete_queue_t
{
    uint16_t object_id;
    struct server_object_delete_queue_t* next;
};

struct server_state_t
{
    struct server_map_t map;
    int listen_port;
    int accept_socket;
    uint16_t next_client_id;
    uint8_t debug_window;
    volatile uint8_t running;
    char* map_filename;
    char* scenario;
    struct server_python_t server_python;
    struct request_handler_t* handlers;
    pthread_mutex_t runnable_mutex;
    struct server_main_thread_runnable* mt_runnable;
    struct client_state_t* client_states;
    struct client_state_t* client_states_ids;
    struct client_state_t* client_states_user_ids;
    long map_refresh_checks;
    int map_refresh_count;
    long client_update_timer;
    long objects_updated_timer;
    long map_save_timer;
    long cb_timer;
    long map_update_timer;
    struct block_method_schedule_t* block_schedule_check;
    struct callback_method_schedule_t* callback_schedule;
    struct server_screen_t* screens;
    struct server_data_t server_data;
    struct server_data_t server_data_modules;
    uint8_t save_map;
    struct computer_rom_t rom_48k;
    struct computer_rom_t rom_spectranet;
    struct computer_t* computers;
    struct computer_t* computers_hashes;
    struct server_network_device_t* devices;
    pthread_mutex_t computers_mutex;
    struct network_bindings_t network_bindings;
    struct server_bullet_t* bullets;
    struct server_object_delete_queue_t* objects_to_delete;

    uint8_t* client_binary;
    uint16_t client_binary_size;
    uint16_t client_binary_addr;
};

#define MAX_RECEIVING_OBJECTS (16)

extern uint64_t server_tick;

#define client_printf(client, format, ...) printf("%6d | " format, client->client_id __VA_OPT__(,) __VA_ARGS__)
#define server_printf(format, ...) printf("global | %lld | " format, server_tick __VA_OPT__(,) __VA_ARGS__)
#define python_printf(format, ...) printf("    py | " format __VA_OPT__(, __VA_ARGS__))

struct client_proto_send_job_t
{
    ProtoObject** objects;
    uint8_t amount;
    uint8_t optimized;
    struct client_proto_send_job_t* next;
};

struct client_object_sync_queue_t
{
    int object_id;
    uint8_t slot;
    struct server_object_reference_t* ref;
    struct object_prediction_t predictions[OBJECT_PREDICTION_FRAMES];
    UT_hash_handle hh;
};

struct client_state_t
{
    pthread_t thread;
    char user_id[64];
    char user_name[64];
    struct proto_process_t proto;
    uint8_t proto_buffer[2048];
    struct server_state_t* state;
    int client_socket;
    uint8_t inited;
    time_t sync_check_timer;
    uint8_t total_chunks_syncing;
    uint16_t client_id;
    uint16_t client_object;
    uint16_t control_object;
    PyObject* py;
    uint8_t watch_active;
    uint16_t watch_x;
    uint16_t watch_y;
    uint16_t team_id;
    uint8_t sync_throttle;
    struct server_main_thread_runnable_wait post_wait;
    int receiving_objects_num;
    ProtoObject* receiving_objects[MAX_RECEIVING_OBJECTS];
    pthread_mutex_t proto_send_jobs_mutex;
    struct client_proto_send_job_t* proto_send_jobs;
    char loaded_module_names[SERVER_DATA_MODULE_NAMESPACE_COUNT][SERVER_DATA_MODULE_NAME_SIZE];
    uint16_t synced_objects[MAX_CLIENT_CACHED_OBJECTS];
    uint8_t synced_chunks[CLIENT_MAX_SYNCED_CHUNKS];
    PyObject* py_postponed_touch;
    uint32_t py_postponed_touch_progress;
    struct client_object_sync_queue_t* object_sync_queue;
    long object_sync_time;
    struct client_state_t* next;
    UT_hash_handle hh_name;
    UT_hash_handle hh_id;
};


void client_state_assign_client_id(struct server_state_t* server_state, struct client_state_t* client_state, uint16_t client_id);
void client_state_assign_user_id(struct server_state_t* server_state, struct client_state_t* client_state,
    const char* user_id, const char* user_name);
struct client_state_t* client_state_find_user_id(struct server_state_t* server_state, const char* user_id);
struct client_state_t* client_state_find_id(struct server_state_t* server_state, uint16_t client_id);
void client_state_release_name(struct server_state_t* server_state, struct client_state_t* client_state);
void client_state_release_id(struct server_state_t* server_state, struct client_state_t* client_state);

extern void client_state_update(struct server_state_t* server_state, struct client_state_t* client_state);
extern void client_state_send_proto_objects(struct server_state_t* server_state, struct client_state_t* client_state,
    ProtoObject** objects, uint8_t amount);
extern void client_state_send_proto_one_object(struct server_state_t* server_state, struct client_state_t* client_state,
    ProtoStackObjectProperty* last_property);

enum notify_message_color_t
{
    NOTIFY_MESSAGE_COLOR_REGULAR = 0x07,
    NOTIFY_MESSAGE_COLOR_BRIGHT = 0x07 + 0x40,
    NOTIFY_MESSAGE_COLOR_DANGER = 0x02 + 0x40,
    NOTIFY_MESSAGE_COLOR_WARNING = 0x06 + 0x40
};

extern void client_state_notify_message(struct server_state_t* server_state, struct client_state_t* client_state,
    const char* message, enum notify_message_color_t color);

extern void server_state_client_send_effect(struct client_state_t* state, struct server_state_t* server_state,
    const char* effect, uint16_t x, uint16_t y);

extern void server_state_client_set_watch(struct client_state_t* state, struct server_state_t* server_state,
    uint8_t chunk_x, uint8_t chunk_y);

extern uint16_t server_state_client_active_object(struct client_state_t* state);
extern void server_state_client_set_active_object(struct client_state_t* state, uint16_t object_id);
extern void server_state_client_check_watch(struct client_state_t* state, struct server_state_t* server_state);

extern uint8_t server_state_client_find_synced_object(struct client_state_t* state, struct server_state_t* server_state,
    struct map_object_t* o);
extern void server_state_client_unsync_object(struct client_state_t* state, struct server_state_t* server_state,
    struct map_object_t* o);
extern void server_state_client_sync_state(struct client_state_t* state, struct server_state_t* server_state,
    struct map_object_t* o);
extern void server_state_client_touch_progress(struct server_state_t* server_state,
    struct client_state_t* client_state, uint8_t progress);

extern void server_state_client_sync_stats(struct client_state_t* state, struct server_state_t* server_state);
void server_python_client_set_object_state(struct client_state_t* client_state, enum client_object_state_t state);
void server_python_client_set_object_state_default(struct client_state_t* client_state);

extern int server_state_init(struct server_state_t *state, uint16_t width, uint16_t height,
    const char* scenario, int listen_port, const char *py_debug_host, int py_debug_port);
extern void server_state_generate(struct server_state_t *state, const char* scenario);
extern uint8_t server_state_listen(struct server_state_t* state);
extern void server_state_loop(struct server_state_t* state);
extern uint8_t server_state_refresh_map(struct server_state_t* state);
extern void server_state_schedule_map_refresh(struct server_state_t* state, uint16_t delay);
extern void server_state_cancel_schedule_block_method(struct server_state_t* state, uint16_t x, uint16_t y);
extern void server_state_schedule_block_method(struct server_state_t* state, uint16_t x, uint16_t y, uint16_t time, const char* method);
extern void server_state_schedule_callback(struct server_state_t* state, PyObject* callback, uint16_t time);
extern void server_state_free(struct server_state_t* state);
extern void server_notify_block_update(struct server_state_t* server_state, struct server_map_t* map, uint16_t x, uint16_t y);

extern long server_time();
extern struct server_state_t* get_server_state();

/*
 * Posts a function to be called on main thread
 */
extern void server_state_post_runnable(struct server_state_t* state, main_thread_runnable_cb callback, struct server_main_thread_runnable_args args);

/*
 * Same as above, but waits for the function execution on main thread, to complete
 */
extern const char* server_state_post_runnable_wait(struct server_state_t* state,
    main_thread_runnable_cb callback, struct server_main_thread_runnable_args args,
    struct server_main_thread_runnable_wait* wait);

extern struct computer_t* server_state_computer_new(struct server_state_t* server_state, int namespace_id, const char* hash);
extern struct computer_t* server_state_computer_find_hash(struct server_state_t* server_state, const char* hash);
extern void server_state_computer_free(struct server_state_t* server_state, struct computer_t* computer);

extern struct server_network_device_t* server_state_device_new(struct server_state_t* server_state, int namespace_id, const char* prefix);
extern void server_state_device_listen(struct server_network_device_t* device);
extern void server_state_device_listen_close(struct server_network_device_t* device);
extern struct server_network_device_session_t* server_state_device_session_new(
    struct server_state_t* server_state, struct server_network_device_t* device,
    PyObject* handler);
extern struct server_network_device_session_t* network_device_connect_to(
    struct server_network_device_t* dev, struct network_device_t* to, int port,
    PyObject* handler);
extern void server_state_device_session_free(struct server_state_t* server_state,
    struct server_network_device_t* device, struct server_network_device_session_t* session);
extern void server_state_device_free(struct server_state_t* server_state, struct server_network_device_t* device);

extern struct computer_t* server_state_computer_get(struct server_state_t* server_state, int computer_id);
extern struct computer_t* server_state_computer_find(struct server_state_t* server_state, const char* hostname);
extern void server_state_computer_set_hostname(struct computer_t *computer, const char *hostname);
extern uint8_t* server_state_computer_serialize(struct computer_t *computer, ssize_t* size);
extern void server_state_computer_deserialize(struct computer_t *computer, uint8_t* data, ssize_t size);

#endif
