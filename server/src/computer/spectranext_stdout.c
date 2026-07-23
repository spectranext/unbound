#include "computer/spectranext.h"
#include "computer.h"
#include "server.h"
#include "server_python.h"

#include <stdlib.h>
#include <string.h>

struct computer_log_message_t
{
    struct computer_t* computer;
    char* message;
};

static const char* spectranext_stdout_flush_buffer_main_thread(struct server_main_thread_runnable_args* args)
{
    struct computer_log_message_t* log_message = args->read.user;

    if (log_message)
    {
        server_python_computer_notify_log_message(log_message->computer, log_message->message);
        free(log_message->message);
        free(log_message);
    }

    return NULL;
}

static void spectranext_stdout_flush_buffer(struct computer_t* computer)
{
    if (!computer->stdout_buffer_length)
        return;

    computer->stdout_buffer[computer->stdout_buffer_length] = '\0';

    struct computer_log_message_t* log_message = calloc(1, sizeof(struct computer_log_message_t));
    log_message->computer = computer;
    log_message->message = strdup(computer->stdout_buffer);

    struct server_main_thread_runnable_args args = {};
    args.read.user = log_message;
    server_state_post_runnable(computer->server_state, spectranext_stdout_flush_buffer_main_thread, args);

    computer->stdout_buffer_length = 0;
}

void spectranext_stdout_write(struct computer_t* computer, uint16_t port, uint8_t data)
{
    (void)port;

    if (computer->stdout_buffer_length >= SPECTRANEXT_STDOUT_BUFFER_SIZE - 1)
        spectranext_stdout_flush_buffer(computer);

    computer->stdout_buffer[computer->stdout_buffer_length++] = (char)data;

    if (data == '\n')
        spectranext_stdout_flush_buffer(computer);
}
