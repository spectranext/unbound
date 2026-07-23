#include <string.h>
#include "system.h"
#include "zxgui.h"
#include "soundfx.h"
#include "server.h"
#include "auth.inc.h"
#include "loader.h"
#include "spectranet.h"
#include "launcher_api.h"

// bottom of the data
__at(61536) uint8_t servers_buffer[4000];

char* target_server = NULL;

extern void switch_alert(const char* progress_message);

static void connect_cb()
{
    soundfx(FX_ITEM_2);

    setpagea(SPECTRANET_LOADER_PAGE0);
    setpageb(SPECTRANET_LOADER_PAGE1);

    launcher_magic = 1337;
    strcpy(launcher_server_addr, target_server + 2);
    launcher_server_port = *(uint16_t*)target_server;

    int err = loader_load_server(target_server + 2, launcher_server_port);

    if (err == -1)
    {
        // if execution wasn't interrupted - something is wrong
        switch_alert("Failed to resolve domain");
    }
    else
    {
        switch_alert("Failed to connect");
    }
}

static uint8_t* get_servers_list(void)
{
    return servers_buffer;
}

static void server_selected(struct gui_select_option_t* selected)
{
    target_server = selected->user;
}

static uint8_t added;

static uint8_t warning_icon[8] = {
    0x00,
    0x18,
    0x18,
    0x18,
    0x18,
    0x00,
    0x18,
    0x00,
};

void switch_auth()
{
    added = 0;

    servers_list.options_size = 0;
    servers_list.buffer_offset = 16 * sizeof(struct gui_select_option_t*);

    for (uint8_t i = 0; i < MAX_SERVERS; i++)
    {
        struct server_t* server = &servers[i];

        if (server->title[0] == '\0')
            continue;

        uint8_t* addr = zxgui_select_add_option(
            &servers_list, server->title, strlen(server->title), strlen(server->address) + 3,
            server->icon, server->icon_color);

        if (target_server == NULL)
        {
            target_server = (char*)addr;
        }

        memcpy(addr, &server->port, 2);
        addr += 2;
        strcpy((char*)addr, server->address);

        added = 1;
    }

    if (!added)
    {
        zxgui_select_add_option(&servers_list, "No servers online, please come back later", 41, 0,
            warning_icon, INK_WHITE);
    }

    zxgui_set_dirty(servers_list);

    zxgui_scene_set(&scene);
}