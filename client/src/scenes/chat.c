#include "system.h"
#include "zxgui.h"
#include "scenes.h"
#include "client_graphics.h"
#include "client.h"
#include "state.h"
#include "messages.h"
#include "soundfx.h"
#include "modules.h"

static void send_msg();
extern char send_buffer[64];

static struct gui_scene_t scene;
static struct gui_label_t chat = zxgui_label_init(NULL, 0, 1, 32, 1, "Chat", INK_CYAN | BRIGHT | PAPER_BLACK, 0);
static struct gui_edit_t send_text = zxgui_multiline_edit_init(NULL, 0, 18, 14, 5, send_buffer, 64);
static struct gui_button_t send_message = zxgui_button_init(NULL, 0, 23, 14, 1, 13, GUI_ICON_RETURN, "Send", send_msg);

static void send_msg()
{
    if (send_buffer[0])
    {
        soundfx(FX_ITEM_2);

        declare_str_property_on_stack(req_id, OBJ_PROPERTY_ID, MSG_CLIENT_CHAT, NULL);
        declare_str_property_on_stack(_m, 'm', send_buffer, &req_id);
        declare_object_on_stack(request, 128, &_m);

        proto_send_one_nf(request);

        send_buffer[0] = '\0';
        send_text.base.flags |= GUI_FLAG_DIRTY;
        panel_close();
    }
    else
    {
        soundfx(FX_ITEM_4);
        panel_close();
    }
}

static void refresh_chat()
{
    send_text.base.next = NULL;

    uint8_t h = 2;

    uint8_t i = msg_index;
    while (1)
    {
        if (i == 0)
        {
            i = MAX_MESSAGES;
        }

        i--;

        const char* msg = client_map_b.chat_messages[i].msg;
        if (*msg == 0)
        {
            break;
        }

        uint8_t height = zxgui_label_text_height(15, msg, strlen(msg), 4);
        height++;

        struct gui_label_t* l = &client_map_b.chat_messages[i].label;
        struct gui_label_t ll = zxgui_label_init(NULL, 17, 1, 15, 0, NULL, INK_WHITE | BRIGHT | PAPER_BLACK, GUI_FLAG_MULTILINE);
        *l = ll;
        l->base.basics.x = 0;
        l->base.basics.y = h;
        l->base.basics.h = height;
        l->title = msg;
        zxgui_scene_add(&scene, l);

        h += height;

        if (i == msg_index)
        {
            break;
        }
    }
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

    if (current_scene == &scene)
    {
        refresh_chat();
    }
}

void init_chat()
{
    memset(send_buffer, 0, sizeof(send_buffer));

    zxgui_scene_init(&scene);
    {
        zxgui_scene_add(&scene, &chat);
    }
    {
        zxgui_scene_add(&scene, &send_message);
    }
    {
        zxgui_scene_add(&scene, &send_text);
    }

    zxgui_scene_set_focus(&scene, &send_text);
}

void switch_chat()
{
    refresh_chat();
    current_scene_module = MODULE_NONE;
    zxgui_scene_set(&scene);
}
