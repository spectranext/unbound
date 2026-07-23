#include "network.h"
#include "server.h"
#include "computer/compat.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/un.h>
#include <sys/socket.h>

extern void* network_accept_thread(void* ctx);

uint8_t network_is_ip_local(uint32_t ip)
{
    return (ip & 0xff000000) == 0x0a000000;
}

uint16_t network_get_device_id_form_local_ip(uint32_t ip)
{
    return ip & 0xffff;
}

uint32_t network_get_local_ip_from_device(struct network_device_t* device)
{
    /*
     a.b.c.d
     ^ a - 10
       ^ b - device type
         ^ ^ c.d - device id
    */
    return 0x0a000000 | ((uint32_t)device->device_type << 16) | device->device_id;
}

void network_device_init(struct network_device_t *device, uint16_t namespace_id, enum device_type_t device_type,
    struct network_bindings_t *bindings)
{
    memset(device, 0, sizeof(struct network_device_t));

    device->device_id = bindings->next_device++;
    device->namespace_id = namespace_id;
    device->device_type = device_type;
    device->bindings = bindings;

    pthread_mutex_lock(&device->bindings->mutex);
    HASH_ADD(hh_id, device->bindings->ids, device_id, sizeof(uint16_t), device);
    pthread_mutex_unlock(&device->bindings->mutex);
}

void network_device_destroy(struct network_device_t* device)
{
    pthread_mutex_lock(&device->bindings->mutex);
    HASH_DELETE(hh_id, device->bindings->ids, device);
    pthread_mutex_unlock(&device->bindings->mutex);

    network_device_unassign_hostname(device);
}

void network_device_assign_hostname(struct network_device_t* device, const char* hostname)
{
    if (strlen(hostname) >= NETWORK_DEVICE_HOSTNAME_SIZE)
        return;

    if (strlen(hostname) == 0)
        return;

    struct network_device_t* exists = network_device_find(device->bindings, hostname);
    if (exists)
        return;

    pthread_mutex_lock(&device->bindings->mutex);

    if (device->hostname_assigned)
    {
        // un-assign
        HASH_DEL(device->bindings->hostnames, device);
    }
    else
    {
        device->hostname_assigned = 1;
    }

    // assign
    strncpy(device->hostname, hostname, sizeof(device->hostname));
    HASH_ADD_STR(device->bindings->hostnames, hostname, device);

    pthread_mutex_unlock(&device->bindings->mutex);
}

void network_device_unassign_hostname(struct network_device_t* device)
{
    if (device->hostname_assigned == 0)
        return;

    pthread_mutex_lock(&device->bindings->mutex);
    HASH_DEL(device->bindings->hostnames, device);
    pthread_mutex_unlock(&device->bindings->mutex);

    device->hostname_assigned = 0;
}

struct network_device_t* network_device_find(struct network_bindings_t* bindings, const char* hostname)
{
    pthread_mutex_lock(&bindings->mutex);
    struct network_device_t* result = NULL;
    HASH_FIND_STR(bindings->hostnames, hostname, result);
    pthread_mutex_unlock(&bindings->mutex);
    return result;
}

extern void* bindings_process_messages(void* ctx);

void network_bindings_init(struct network_bindings_t* bindings, struct server_state_t* server_state)
{
    memset(bindings, 0, sizeof (struct network_bindings_t));
    bindings->next_device = 1;
    bindings->server_state = server_state;
    pthread_mutex_init(&bindings->mutex, NULL);
    pthread_mutex_init(&bindings->read_mutex, NULL);

    pthread_mutex_init(&bindings->messages_mutex, NULL);
    pthread_cond_init(&bindings->messages_cond, NULL);

    pthread_create(&bindings->read_thread, NULL, network_accept_thread, bindings);
    pthread_create(&bindings->messages_thread, NULL, bindings_process_messages, bindings);
}

void network_bindings_destroy(struct network_bindings_t* bindings)
{
    bindings_shutdown_messages(bindings);
    pthread_mutex_destroy(&bindings->messages_mutex);
    pthread_cond_destroy(&bindings->messages_cond);
    pthread_mutex_destroy(&bindings->mutex);
    pthread_mutex_destroy(&bindings->read_mutex);
}

struct network_device_t* network_lookup_device(struct network_device_t* device, const char* hostname)
{
    pthread_mutex_lock(&device->bindings->mutex);
    struct network_device_t* other = NULL;
    HASH_FIND_STR(device->bindings->hostnames, hostname, other);
    pthread_mutex_unlock(&device->bindings->mutex);

    if (other == NULL)
    {
        return 0;
    }

    if (other->namespace_id && (other->namespace_id != device->namespace_id))
    {
        return 0;
    }

    return other;
}

struct network_device_t* network_lookup_device_ip(struct network_device_t* device, uint32_t ip)
{
    uint16_t device_id = network_get_device_id_form_local_ip(ip);

    pthread_mutex_lock(&device->bindings->mutex);
    struct network_device_t* other = NULL;
    HASH_FIND(hh_id, device->bindings->ids, &device_id, sizeof(device_id), other);
    pthread_mutex_unlock(&device->bindings->mutex);

    return other;
}

extern void w5100_socket_unix_gen_path(uint16_t device_id,
    uint16_t namespace_id, uint16_t port, struct sockaddr_un* un);

int network_device_listen(struct network_device_t* dev)
{
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0)
        return sock;

    struct sockaddr_un un;
    w5100_socket_unix_gen_path(dev->device_id, dev->namespace_id, 1, &un);

    unlink(un.sun_path);

    if (bind(sock, (struct sockaddr*)&un, sizeof(un)) == -1) {
        server_printf(
            "failed to bind for device %d socket %d; errno %d: %s\n",
            dev->device_id, sock, compat_socket_get_error(),
            compat_socket_get_strerror() );
        close(sock);
        return -1;
    }

    chmod(un.sun_path, 0777);

    int res = listen(sock, 10);
    if (res < 0)
    {
        server_printf(
            "failed to listen for device %d on socket %d; errno %d: %s\n",
            dev->device_id, sock, compat_socket_get_error(),
            compat_socket_get_strerror() );
        close(sock);
        return res;
    }

    server_printf("listen for device %d on socket %d at path %s\n", sock, dev->device_id, un.sun_path);

    return sock;
}