#ifndef NETWORK_H_
#define NETWORK_H_

#include <inttypes.h>
#include <pthread.h>
#include <ut/uthash.h>

struct server_state_t;

struct network_msg_t
{
    struct network_device_t*            to;
    uint8_t*                            message;
    uint16_t                            message_length;
    int                                 port;

    struct network_msg_t*               next;
    struct network_msg_t*               prev;
};

enum device_type_t
{
    DEVICE_COMPUTER = 0,
    DEVICE_OTHER
};

#define NETWORK_DEVICE_HOSTNAME_SIZE    64
#define NETWORK_MESSAGES_PORT           1

struct network_device_t
{
    uint16_t                            device_id;
    uint16_t                            namespace_id;
    char                                hostname[NETWORK_DEVICE_HOSTNAME_SIZE];
    uint8_t                             hostname_assigned;

    enum device_type_t                  device_type;
    struct network_bindings_t*          bindings;

    UT_hash_handle                      hh;
    UT_hash_handle                      hh_id;
};

#define NETWORK_MAX_NET_DEVICES         4096
#define NETWORK_READ_MAX_SIZE           4096

struct network_device_fd_t;

struct network_bindings_t
{
    uint16_t                            next_device;
    pthread_mutex_t                     mutex;

    uint8_t                             run_servers;

    struct network_device_t*            hostnames;
    struct network_device_t*            ids;

    struct network_msg_t*               inbound_messages;
    uint8_t                             messages_running;
    pthread_t                           messages_thread;
    pthread_mutex_t                     messages_mutex;
    pthread_cond_t                      messages_cond;

    struct network_device_fd_t*         accepts;
    struct network_device_fd_t*         reads;
    pthread_t                           read_thread;
    char                                read_buffer[NETWORK_READ_MAX_SIZE];
    pthread_mutex_t                     read_mutex;

    struct server_state_t*              server_state;
};

extern void network_bindings_init(struct network_bindings_t* bindings, struct server_state_t* server_state);
extern void network_bindings_destroy(struct network_bindings_t* bindings);

extern void network_device_init(struct network_device_t *device, uint16_t namespace_id, enum device_type_t device_type,
    struct network_bindings_t *bindings);
extern void network_device_destroy(struct network_device_t* device);

extern void network_device_assign_hostname(struct network_device_t* device, const char* hostname);
extern void network_device_unassign_hostname(struct network_device_t* device);
extern struct network_device_t* network_device_find(struct network_bindings_t* bindings, const char* hostname);

extern uint8_t network_is_ip_local(uint32_t ip);
extern uint16_t network_get_device_id_form_local_ip(uint32_t ip);
extern uint32_t network_get_local_ip_from_device(struct network_device_t* device);

extern struct network_device_t* network_lookup_device(struct network_device_t* device, const char* hostname);
extern struct network_device_t* network_lookup_device_ip(struct network_device_t* device, uint32_t ip);

struct server_main_thread_runnable_args;
typedef const char* (*main_thread_runnable_cb)(struct server_main_thread_runnable_args* args);

int network_device_listen(struct network_device_t* dev);
void network_add_accept_device(struct network_bindings_t* bindings, int fd, main_thread_runnable_cb cb, void* user);
void network_add_read_device(struct network_bindings_t* bindings, int fd, main_thread_runnable_cb cb, void* user);
void network_remove_accept_device(struct network_bindings_t* bindings, int fd);
void network_remove_read_device(struct network_bindings_t* bindings, int fd);

extern void bindings_add_message(struct network_bindings_t* bindings, struct network_device_t* to,
    int port, uint8_t length, const uint8_t* message);
extern void bindings_shutdown_messages(struct network_bindings_t* bindings);

#endif