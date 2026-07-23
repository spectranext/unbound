#include "network.h"
#include <poll.h>
#include "server.h"
#include <ut/utlist.h>
#include <sys/socket.h>

struct network_device_fd_t
{
    int                                 fd;
    void*                               user;
    main_thread_runnable_cb             callback;
    struct network_device_fd_t*         next;
    struct network_device_fd_t*         prev;
};

void network_add_accept_device(struct network_bindings_t* bindings, int fd, main_thread_runnable_cb cb, void* user)
{
    pthread_mutex_lock(&bindings->read_mutex);
    struct network_device_fd_t* f = calloc(1, sizeof(struct network_device_fd_t));
    f->fd = fd;
    f->callback = cb;
    f->user = user;
    DL_APPEND(bindings->accepts, f);
    pthread_mutex_unlock(&bindings->read_mutex);
}

void network_add_read_device(struct network_bindings_t* bindings, int fd, main_thread_runnable_cb cb, void* user)
{
    pthread_mutex_lock(&bindings->read_mutex);
    struct network_device_fd_t* f = calloc(1, sizeof(struct network_device_fd_t));
    f->fd = fd;
    f->callback = cb;
    f->user = user;
    DL_APPEND(bindings->reads, f);
    pthread_mutex_unlock(&bindings->read_mutex);
}

void network_remove_accept_device(struct network_bindings_t* bindings, int fd)
{
    pthread_mutex_lock(&bindings->read_mutex);
    struct network_device_fd_t* el;
    struct network_device_fd_t* tmp;
    DL_FOREACH_SAFE(bindings->accepts, el, tmp)
    {
        if (el->fd == fd)
        {
            DL_DELETE(bindings->accepts, el);
            free(el);
            break;
        }
    }
    pthread_mutex_unlock(&bindings->read_mutex);
}

void network_remove_read_device(struct network_bindings_t* bindings, int fd)
{
    pthread_mutex_lock(&bindings->read_mutex);
    struct network_device_fd_t* el;
    struct network_device_fd_t* tmp;
    DL_FOREACH_SAFE(bindings->reads, el, tmp)
    {
        if (el->fd == fd)
        {
            DL_DELETE(bindings->reads, el);
            free(el);
            break;
        }
    }
    pthread_mutex_unlock(&bindings->read_mutex);
}

void* network_accept_thread(void* ctx)
{
    struct network_bindings_t* bindings = ctx;

    struct pollfd* pollfds = calloc(NETWORK_MAX_NET_DEVICES + 2, sizeof(struct pollfd));
    struct network_device_fd_t** handlers = calloc(NETWORK_MAX_NET_DEVICES + 2, sizeof(struct network_device_fd_t*));

    bindings->run_servers = 1;

    while (bindings->run_servers)
    {
        pthread_mutex_lock(&bindings->read_mutex);

        struct network_device_fd_t* el;
        int idx = 0;

        // add accepts
        DL_FOREACH(bindings->accepts, el)
        {
            pollfds[idx].fd = el->fd;
            pollfds[idx].events = POLLIN;
            pollfds[idx].revents = 0;
            handlers[idx] = el;

            idx++;
        }

        int accepts = idx;

        // add reads
        DL_FOREACH(bindings->reads, el)
        {
            pollfds[idx].fd = el->fd;
            pollfds[idx].events = POLLIN | POLLHUP | POLLERR;
            pollfds[idx].revents = 0;
            handlers[idx] = el;

            idx++;
        }

        pthread_mutex_unlock(&bindings->read_mutex);

        if (idx == 0)
        {
            usleep(20000);
            continue;
        }

        int pollResult = poll(pollfds, idx, 2000);

        if (pollResult < 0)
        {
            server_printf("poll failure");
            break;
        }

        if (pollResult == 0)
        {
            continue;
        }

        pthread_mutex_lock(&bindings->read_mutex);

        for (int i = 0; i < idx; i++)
        {
            uint8_t is_accept = i < accepts;

            if (is_accept)
            {
                if (pollfds[i].revents & POLLIN)
                {
                    // a new connection was accepted
                    int client_socket = accept(handlers[i]->fd, NULL, NULL);
                    struct server_main_thread_runnable_args args = {};
                    args.accept.client_socket = client_socket;
                    args.accept.user = handlers[i]->user;
                    server_state_post_runnable(bindings->server_state, handlers[i]->callback, args);
                }
            }
            else
            {
                if (pollfds[i].revents & POLLIN)
                {
                    // existing connection received something
                    long buffer_size = read(pollfds[i].fd, bindings->read_buffer, sizeof(bindings->read_buffer));
                    if (buffer_size <= 0)
                    {
                        // connection was closed
                        struct server_main_thread_runnable_args args = {};
                        args.read.user = handlers[i]->user;
                        args.read.data = NULL;
                        args.read.data_length = 0;
                        server_state_post_runnable(bindings->server_state, handlers[i]->callback, args);

                        DL_DELETE(bindings->reads, handlers[i]);
                        free(handlers[i]);
                        continue;
                    }
                    else
                    {
                        char *b = malloc(buffer_size);
                        memcpy(b, bindings->read_buffer, buffer_size);
                        struct server_main_thread_runnable_args args = {};
                        args.read.user = handlers[i]->user;
                        args.read.data = (uint8_t*)b;
                        args.read.data_length = buffer_size;
                        server_state_post_runnable(bindings->server_state, handlers[i]->callback, args);
                    }
                }

                if (pollfds[i].revents & (POLLHUP | POLLERR))
                {
                    // connection was closed
                    struct server_main_thread_runnable_args args = {};
                    args.read.user = handlers[i]->user;
                    args.read.data = NULL;
                    args.read.data_length = 0;
                    server_state_post_runnable(bindings->server_state, handlers[i]->callback, args);

                    DL_DELETE(bindings->reads, handlers[i]);
                    free(handlers[i]);
                }
            }
        }

        pthread_mutex_unlock(&bindings->read_mutex);
    }

    free(pollfds);
    free(handlers);

    return NULL;
}