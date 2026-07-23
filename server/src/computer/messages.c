#include <arpa/inet.h>
#include <sys/un.h>
#include "computer.h"
#include "server.h"
#include "network.h"

static void bindings_messages_free(struct network_msg_t* messages);

extern void w5100_socket_unix_gen_path(uint16_t device_id,
    uint16_t namespace_id, uint16_t port, struct sockaddr_un* un);

void* bindings_process_messages(void* ctx)
{
    struct network_bindings_t* bindings = ctx;

    bindings->messages_running = 1;

    while (bindings->messages_running)
    {
        pthread_mutex_lock(&bindings->messages_mutex);

        while ((bindings->inbound_messages == NULL) && (bindings->messages_running))
        {
            pthread_cond_wait(&bindings->messages_cond, &bindings->messages_mutex);
        }

        struct network_msg_t* messages = bindings->inbound_messages;
        bindings->inbound_messages = NULL;

        pthread_mutex_unlock(&bindings->messages_mutex);

        if (bindings->messages_running == 0)
            break;

        int inbound_socket = socket(AF_UNIX, SOCK_STREAM, 0);
        if (inbound_socket < 0)
        {
            server_printf("cpu-msg: could not allocate a socket\n");
            bindings_messages_free(messages);
        }
        else
        {
            struct network_msg_t* el;
            struct network_msg_t* tmp;

            int delivered = 0;

            DL_FOREACH_SAFE(messages, el, tmp)
            {
                struct sockaddr_un un;
                w5100_socket_unix_gen_path(el->to->device_id, el->to->namespace_id, el->port, &un);

                if (connect(inbound_socket, (struct sockaddr*)&un, sizeof(un)) < 0)
                {
                    server_printf("cpu-msg: messages: cpu %s is not listening (could not connect)\n", el->to->hostname);
                }
                else
                {
                    send(inbound_socket, el->message, el->message_length, 0);
                    shutdown(inbound_socket, 0);
                    close(inbound_socket);
                }
                
                delivered++;
                DL_DELETE(messages, el);
                free(el->message);
                free(el);
            }

            server_printf("cpu-msg: delivered %d messages\n", delivered);
        }
    }

    return NULL;
}

void bindings_add_message(struct network_bindings_t* bindings, struct network_device_t* to,
    int port, uint8_t length, const uint8_t* message)
{
    pthread_mutex_lock(&bindings->messages_mutex);

    struct network_msg_t* msg = calloc(1, sizeof(struct network_msg_t));
    msg->to = to;
    msg->port = port;
    msg->message = malloc(length);
    msg->message_length = length;
    memcpy(msg->message, message, length);

    DL_APPEND(bindings->inbound_messages, msg);

    pthread_cond_signal(&bindings->messages_cond);
    pthread_mutex_unlock(&bindings->messages_mutex);
}

void bindings_shutdown_messages(struct network_bindings_t* bindings)
{
    bindings->messages_running = 0;
    pthread_mutex_lock(&bindings->messages_mutex);
    pthread_cond_signal(&bindings->messages_cond);
    pthread_mutex_unlock(&bindings->messages_mutex);
    pthread_join(bindings->messages_thread, NULL);

    bindings->messages_thread = NULL;
}

static void bindings_messages_free(struct network_msg_t* messages)
{
    struct network_msg_t* el;
    struct network_msg_t* tmp;

    DL_FOREACH_SAFE(messages, el, tmp)
    {
        DL_DELETE(messages, el);
        free(el);
    }
}