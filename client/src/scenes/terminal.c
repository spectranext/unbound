#include "system.h"
#include "zxgui.h"
#include "scenes.h"
#include "client_graphics.h"
#include "client.h"
#include "state.h"
#include "messages.h"
#include "soundfx.h"
#include "notifications.h"
#include "hud.h"

static void send_msg();
extern char send_buffer[64];

static struct gui_edit_t send_text = zxgui_multiline_edit_init(NULL, 2, 23, 16, 1, send_buffer, 64);
static struct gui_button_t send_message = zxgui_button_init(&send_text.base, 0, 23, 2, 1, 13, GUI_ICON_RETURN, "> ", send_msg);
static struct gui_scene_t scene = {&send_message.base, &send_text.base};

static void send_msg()
{
    if (send_buffer[0])
    {
        soundfx(FX_ITEM_2);

        declare_str_property_on_stack(req_id, OBJ_PROPERTY_ID, MSG_TERMINAL, NULL);
        declare_str_property_on_stack(_m, 'm', send_buffer, &req_id);
        declare_object_on_stack(request, 128, &_m);

        proto_send_one_nf(request);
    }
    else
    {
        soundfx(FX_ITEM_4);
    }

    memset(send_buffer, 0, sizeof(send_buffer));
    zxgui_screen_color(INK_WHITE | PAPER_BLACK);
    zxgui_screen_clear(0, 23, 32, 1);

    zxgui_scene_set(NULL);
    notification_state = NOTIFICATION_STATE_NONE;
    control_mode = CONTROL_MODE_MOVE;
    update_target_marker();
    my_stats_dirty = 1;
}

void init_terminal()
{
}

void switch_terminal()
{
    notification_state = NOTIFICATION_STATE_BLOCKED;
    control_mode = CONTROL_MODE_PANEL;
    disable_target_marker();
    zxgui_scene_set(&scene);
}