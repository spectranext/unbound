#include "client.h"
#include "client_map.h"
#include <proto_req.h>
#include "state.h"
#include <spectranet.h>
#include <spectrum.h>
#include "scenes.h"
#include "zxgui.h"
#include "messages.h"
#include "client_data.h"
#include "client_graphics.h"
#include "hud.h"
#include "client_net.h"
#include "client_auth.h"

static uint8_t disconnected_alert_visible = 0;

static void disconnected()
{
    if (disconnected_alert_visible)
    {
        return;
    }

    disconnected_alert_visible = 1;
    state_active_phase = 0;
    rendering_blocked = 1;
    skip_rendering_while_dirty = 0;
    control_mode = CONTROL_MODE_PANEL;
    disable_target_marker();
    switch_alert("Disconnected from server. Press RETURN to reconnect, SPACE to reboot.");
    zxgui_scene_iteration();
}

extern char server_connect_addr[64];
extern uint16_t server_connect_port;

void do_connect()
{
    client_connect(server_connect_addr, server_connect_port);
}

void restart_to_main()
{
    proto_disconnect();
#ifndef __CLION_IDE__
#asm
    di
    jp 25000
#endasm
#endif
}

void client_connect(const char* address, uint16_t port)
{
    proto_req_init_processor(client_new_message, client_message_object, client_message_complete, NULL);

    disconnected_alert_visible = 0;
    switch_alert("Connecting to server...");
    zxgui_scene_iteration();

    if (proto_connect(address, port, disconnected) >= 0)
    {
        client_auth();
    }
    else
    {
        switch_alert("Could not connect to the server. Make sure the server is running and your device has internet connectivity.");
    }
}

void panel_open()
{
    panel = 1;
    rendering_blocked = 1;
    zxgui_clear();
    control_mode = CONTROL_MODE_PANEL;
    disable_target_marker();
}

void panel_close()
{
    zxgui_scene_set(NULL);
    panel = 0;
    rendering_blocked = 0;
    client_map_b.screen_dirty = 1;
    control_mode = CONTROL_MODE_MOVE;
    update_target_marker();
    my_stats_dirty = 1;
}

void update_camera_bounds()
{
    camera_x_end = camera_x + 4;
    camera_y_end = camera_y + 3;

    camera_base_x = (uint16_t)camera_x * MAP_CHUNK_SIZE;
    camera_base_y = (uint16_t)camera_y * MAP_CHUNK_SIZE;

    camera_low_phy_x = OBJECT_LOGICAL_TO_PHY(camera_base_x);
    camera_low_phy_y = OBJECT_LOGICAL_TO_PHY(camera_base_y + 1);
    camera_high_phy_x = camera_low_phy_x + OBJECT_LOGICAL_TO_PHY(30);
    camera_high_phy_y = camera_low_phy_y + OBJECT_LOGICAL_TO_PHY(22);
}

void get_objects_a() __naked
{
#ifndef __CLION_IDE__
#asm
    extern SETPAGEA
    ld a, SPECTRANET_OBJECTS_PAGE
    jp SETPAGEA
#endasm
#endif
}
