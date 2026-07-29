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
#include "modules.h"

enum terminal_mode_t
{
    TERMINAL_MODE_TERMINAL = 0,
    TERMINAL_MODE_CHAT,
};

static void send_msg();
extern char send_buffer[64];

static enum terminal_mode_t terminal_mode = TERMINAL_MODE_TERMINAL;
static struct gui_edit_t send_text = zxgui_multiline_edit_init(NULL, 4, 23, 14, 1, send_buffer, 64);
static struct gui_button_t send_message = zxgui_button_init(&send_text.base, 0, 23, 2, 1, 13, GUI_ICON_RETURN, "term> ", send_msg);
static struct gui_scene_t scene = {&send_message.base, &send_text.base};

static void send_ui_blocked(uint8_t blocked) __z88dk_fastcall
{
    declare_str_property_on_stack(req_id, OBJ_PROPERTY_ID, MSG_UI_BLOCKED, NULL);
    declare_arg_property_on_stack(_b, 'b', blocked, &req_id);
    declare_object_on_stack(request, 32, &_b);

    proto_send_one_nf(request);
}

static void send_msg()
{
    if (send_buffer[0])
    {
        soundfx(FX_ITEM_2);

        declare_str_property_on_stack(req_id, OBJ_PROPERTY_ID, terminal_mode == TERMINAL_MODE_CHAT ? MSG_CLIENT_CHAT : MSG_TERMINAL, NULL);
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

    send_ui_blocked(0);
    module_scene_clear();
    notification_state = NOTIFICATION_STATE_NONE;
    control_mode = CONTROL_MODE_MOVE;
    update_target_marker();
    my_stats_dirty = 1;
}

void char_msg(const char* msg, uint16_t len) __z88dk_fastcall
{
    char* dest = client_map_b.chat_messages[msg_index++].msg;

    if (msg_index >= MAX_MESSAGES)
    {
        msg_index = 0;
    }

    memcpy(dest, msg, len);
    dest += len;
    *dest = 0;
}

void init_terminal()
{
    memset(send_buffer, 0, sizeof(send_buffer));
}

static void switch_terminal_mode(enum terminal_mode_t mode) __z88dk_fastcall
{
    terminal_mode = mode;
    send_message.title = terminal_mode == TERMINAL_MODE_CHAT ? "chat> " : "term> ";
    send_message.base.flags |= GUI_FLAG_DIRTY;
    send_text.base.flags |= GUI_FLAG_DIRTY;
    memset(send_buffer, 0, sizeof(send_buffer));

    notification_state = NOTIFICATION_STATE_BLOCKED;
    control_mode = CONTROL_MODE_PANEL;
    disable_target_marker();
    current_scene_module = MODULE_NONE;
    send_ui_blocked(1);
    zxgui_scene_set(&scene);
}

void switch_terminal()
{
    switch_terminal_mode(TERMINAL_MODE_TERMINAL);
}

void switch_chat()
{
    switch_terminal_mode(TERMINAL_MODE_CHAT);
}
