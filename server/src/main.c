#include <stdio.h>
#include <signal.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

#include "server_map.h"
#include "debug_window.h"
#include "server.h"
#include "utils.h"

struct server_state_t server_state;

struct server_state_t* get_server_state()
{
    return &server_state;
}

void sig_stop(int sig_num)
{
    server_state.running = 0;
}

int main(int argc, const char** argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    signal(SIGINT, sig_stop);
    srand(time(NULL));

    const char* py_debug_host = NULL;
    const char* map = NULL;
    const char* scenario = "default";
    int py_debug_port = 0;
    int listen_port = 13390;
    int map_width = 64;
    int map_height = 16;
    uint8_t debug_window = 0;
    uint8_t save_map = 1;

    for (int i = 1; i < argc; i++)
    {
        const char* arg = argv[i];

        if (strcmp(arg, "--port") == 0)
        {
            listen_port = atoi(argv[++i]);
        }
        else if (strcmp(arg, "--py-debug") == 0)
        {
            py_debug_host = argv[++i];
            py_debug_port = atoi(argv[++i]);
        }
        else if (strcmp(arg, "--map") == 0)
        {
            map = argv[++i];
        }
        else if (strcmp(arg, "--scenario") == 0)
        {
            scenario = argv[++i];
        }
        else if (strcmp(arg, "--map-width") == 0)
        {
            map_width = atoi(argv[++i]);
        }
        else if (strcmp(arg, "--map-height") == 0)
        {
            map_height = atoi(argv[++i]);
        }
        else if (strcmp(arg, "--debug-window") == 0)
        {
            debug_window = 1;
        }
        else if (strcmp(arg, "--dont-save-map") == 0)
        {
            save_map = 0;
        }
        else
        {
            server_printf("Warning: unknown option: %s\n", arg);
        }
    }

    int e = server_state_init(&server_state, map_width, map_height, scenario,
                              listen_port, py_debug_host, py_debug_port);

    if (e)
    {
        server_printf("Cannot init server state (%d).\n", e);
        return 1;
    }

    if (server_state_listen(&server_state))
    {
        server_printf("Cannot listen.\n");
        return 2;
    }

    if (debug_window)
    {
        debug_window_init();
    }

    server_state.debug_window = debug_window;
    server_state.save_map = save_map;

    if (map == NULL)
    {
        map = "map.proto";
        server_printf("Map is not specified, chosen %s.\n", map);
    }

    if (file_exists(map))
    {
        if (server_map_load(&server_state, &server_state.map, map))
        {
            server_printf("Cannot load map: %s.\n", map);
            return 4;
        }

        server_python_map_refresh(&server_state.server_python);
        server_python_map_yield_chunks(&server_state, &server_state.server_python);
    }
    else
    {
        server_state_generate(&server_state, scenario);
        if (save_map)
        {
            server_map_save(&server_state, &server_state.map, map);
        }
    }

    server_python_map_init(&server_state.server_python, scenario);

    server_state.map_filename = strdup(map);

    server_state_loop(&server_state);

    server_printf("Server loop is done.\n");

    server_state_free(&server_state);

    if (debug_window)
    {
        debug_window_free();
    }

    return 0;
}
