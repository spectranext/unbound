#include "system.h"
#include "zxgui.h"
#include "client_graphics.h"
#include "client.h"
#include "client_map.h"
#include "client_data.h"
#include "state.h"
#include "proto.h"
#include "notifications.h"
#include "messages.h"
#include "soundfx.h"
#include "modules.h"

static uint8_t selected_option_index = 0;
static char message_title[32];
static char message_description[512];
static char action_a[16] = {};
static char action_b[16] = {};
static char cancel_action[16] = "Exit";
static uint8_t has_description = 0;
static uint8_t has_edit = 0;
static uint8_t flags = 0;
static uint8_t quick_cancel = 0;
static uint8_t has_secondary_options = 0;
static uint8_t halt_callback_snd = 0;
static uint8_t selected_option_found = 0;
static uint8_t module_object_index = 0;
static char option_staging[256];

#define FLAG_MESSAGE_TO_SIDE 0x01

static void cb_exit();
static void selected_a();
static void selected_b();
static uint8_t* options_obtain_data();
static uint8_t* options_obtain_secondary_data();
static void options_release_data();
static void options_select(struct gui_select_option_t* selected);

#define OPTIONS_TO_SELECT_CAPACITY 128

static struct gui_scene_t scene;
static struct gui_label_t message = zxgui_label_init(NULL, 0, 1, 32, 1, message_title, INK_CYAN | BRIGHT | PAPER_BLACK, 0);
static struct gui_select_t options_to_select = zxgui_select_init(NULL, 0, 2, 15, 15, options_obtain_data, options_release_data, OPTIONS_TO_SELECT_CAPACITY, NULL, options_select);
static struct gui_select_t secondary_to_select = zxgui_select_init(NULL, 16, 2, 15, 15, options_obtain_secondary_data, options_release_data, OPTIONS_TO_SELECT_CAPACITY, NULL, options_select);
static struct gui_label_t description = zxgui_label_init(NULL, 0, 2, 32, 16, message_description, INK_WHITE | PAPER_BLACK, GUI_FLAG_MULTILINE);
static struct gui_edit_t edit = zxgui_multiline_edit_init(NULL, 0, 2, 32, 4, message_description, 200);
static struct gui_button_t select_a = zxgui_button_init(NULL, 0, 22, 8, 1, 13, GUI_ICON_RETURN, action_a, selected_a);
static struct gui_button_t btn_exit = zxgui_button_init(NULL, 8, 22, 12, 1, 32, GUI_ICON_SPACE, cancel_action, cb_exit);
static struct gui_button_t select_b = zxgui_button_init(NULL, 20, 22, 8, 1, 'c', GUI_ICON_C, action_b, selected_b);

static void select_action(const char* action);

static void selected_a()
{
    if (has_edit)
    {
        select_action(message_description);
    }
    else
    {
        select_action(action_a);
    }
}

static void selected_b()
{
    select_action(action_b);
}

static uint8_t* options_obtain_data()
{
#ifndef __CLION_IDE__
#asm
    extern SETPAGEB
    ld a, SPECTRANET_QUERY_PAGE
    call SETPAGEB
#endasm
#endif
    return (uint8_t*)0x2000;
}

static uint8_t* tile_obtain_data()
{
#ifndef __CLION_IDE__
#asm
    extern SETPAGEB
    ld a, SPECTRANET_TILES_PAGE
    call SETPAGEB
#endasm
#endif
    return (uint8_t*)0x2000;
}

static uint8_t* options_obtain_secondary_data()
{
#ifndef __CLION_IDE__
#asm
    extern SETPAGEB
    ld a, SPECTRANET_QUERY2_PAGE
    call SETPAGEB
#endasm
#endif
    return (uint8_t*)0x2000;
}

static void options_release_data()
{
    client_map_get_b();
}

static void copy_tile_icon_to_staging(uint8_t icon_tile, uint8_t* icon_staging, uint8_t* icon_color)
{
    uint8_t* tiles_b = tile_obtain_data();
    for (uint8_t bit = 0; bit < 8; bit++)
    {
        icon_staging[bit] = tiles_b[icon_tile + (uint16_t)bit * 256];
    }

    *icon_color = tiles_b[(uint16_t)icon_tile + 2048];
    options_release_data();
}

static void options_select(struct gui_select_option_t* selected)
{
    selected_option_index = *(uint8_t*)selected->user;
}

static void sync_selected_option(struct gui_select_t* select)
{
    if (select->options_size)
    {
        zxgui_select_trigger_change_event(select);
    }
}

static uint8_t on_button_pressed(enum gui_event_type event_type, void* event)
{
    if (!has_secondary_options)
        return 0;

    if (event_type == GUI_EVENT_KEY_PRESSED)
    {
        struct gui_event_key_pressed* ev = event;

        switch (ev->key)
        {
            case GUI_KEY_CODE_LEFT:
            case 'o':
            {
                if (scene.focus == (void*)&secondary_to_select)
                {
                    options_to_select.selection = secondary_to_select.selection;
                    if (options_to_select.selection >= options_to_select.options_size)
                    {
                        options_to_select.selection = options_to_select.options_size - 1;
                    }
                }

                zxgui_scene_set_focus(&scene, &options_to_select);
                options_to_select.base.flags |= GUI_FLAG_DIRTY_INTERNAL;
                secondary_to_select.base.flags |= GUI_FLAG_DIRTY_INTERNAL;
                zxgui_select_trigger_change_event(&options_to_select);
                return 1;
            }
            case GUI_KEY_CODE_RIGHT:
            case 'p':
            {
                if (scene.focus == (void*)&options_to_select)
                {
                    secondary_to_select.selection = options_to_select.selection;
                    if (secondary_to_select.selection >= secondary_to_select.options_size)
                    {
                        secondary_to_select.selection = secondary_to_select.options_size - 1;
                    }
                }

                zxgui_scene_set_focus(&scene, &secondary_to_select);
                options_to_select.base.flags |= GUI_FLAG_DIRTY_INTERNAL;
                secondary_to_select.base.flags |= GUI_FLAG_DIRTY_INTERNAL;
                zxgui_select_trigger_change_event(&secondary_to_select);
                return 1;
            }
        }
    }

    return 0;
}

static void init_scene()
{
    zxgui_scene_init(&scene);
    scene.on_event = on_button_pressed;
    zxgui_scene_add(&scene, &message);
}

static void init_query()
{
    init_scene();
}

static uint8_t description_offset = 1;

static void query_object_callback(uint8_t index, ProtoObject* object)
{
    if (index == 0)
    {
        panel_open();
        init_scene();
        module_scene_set(&scene);

        ProtoObjectPropertyPtr* prop = object->properties;

        static uint8_t options_count; options_count = 0;
        description_offset = 2;
        {
            ProtoObjectPropertyPtr* flags_prop = prop;
            while (*flags_prop)
            {
                if ((*flags_prop)->key == 'f')
                {
                    flags = *(*flags_prop)->value;
                    if (flags & FLAG_MESSAGE_TO_SIDE)
                    {
                        description_offset--;
                    }
                    break;
                }
                flags_prop++;
            }
        }

        while (*prop)
        {
            static ProtoObjectProperty* p;
            p = *prop;

            switch (p->key)
            {
                case 'c':
                {
                    selected_option_index = *p->value;
                    selected_option_found = 0;
                    break;
                }
                case 'm':
                {
                    memcpy(message_title, p->value, p->value_size);
                    message_title[p->value_size] = 0;
                    break;
                }
                case 'I':
                {
                    static const uint8_t* d; d = (uint8_t*)p->value;

                    static uint8_t w; w = *d++;
                    static uint8_t h; h = *d++;

                    if (w <= 32 && h <= 24 && (p->value_size == (2 + (w * h * 8) + (w * h))))
                    {
                        uint16_t payload = w * h * 8;
                        const uint8_t* color = d + payload;
                        static uint8_t x;
                        if (flags & FLAG_MESSAGE_TO_SIDE)
                        {
                            x = 22;
                        }
                        else
                        {
                            x = 16;
                        }
                        x -= w / 2;
                        zxgui_image(x, 2, w, h, d, color);
                        description_offset += h + 1;
                    }

                    break;
                }
                case 'd':
                {
                    has_description = 1;
                    if (p->value_size >= sizeof(message_description))
                    {
                        p->value_size = sizeof(message_description) - 1;
                    }

                    memcpy(message_description, p->value, p->value_size);
                    message_description[p->value_size] = 0;
                    description.base.flags = GUI_FLAG_MULTILINE | GUI_FLAG_DIRTY;
                    description.base.basics.y = description_offset;
                    if (flags & FLAG_MESSAGE_TO_SIDE)
                    {
                        description.base.basics.x = 12;
                        description.base.basics.w = 20;
                        description.base.basics.h = 24 - description_offset;
                    }
                    else
                    {
                        description.base.basics.x = 0;
                        description.base.basics.w = 32;
                        uint8_t h = zxgui_label_text_height(description.base.basics.w, p->value, p->value_size, 4) + 1;
                        description.base.basics.h = h;
                        description_offset += h;
                    }
                    break;
                }
                case 's':
                {
                    has_secondary_options = *p->value;
                    break;
                }
                case 'f':
                {
                    break;
                }
                case 'e':
                {
                    has_edit = *p->value;

                    if (has_edit)
                    {
                        message_description[0] = 0;
                        edit.base.flags = GUI_FLAG_DIRTY | GUI_FLAG_MULTILINE;
                        description_offset += 4;
                    }

                    break;
                }
                case 'q':
                {
                    quick_cancel = *p->value;
                    break;
                }
                case 'x':
                {
                    if (p->value_size >= sizeof(cancel_action))
                    {
                        p->value_size = sizeof(cancel_action) - 1;
                    }
                    memcpy(cancel_action, p->value, p->value_size);
                    cancel_action[p->value_size] = 0;
                    btn_exit.base.flags |= GUI_FLAG_DIRTY;
                    break;
                }
                case OBJ_PROPERTY_ID:
                case 'n':
                case 't':
                {
                    // ignore
                    break;
                }
                case 'o':
                // assume o
                default:
                {
                    switch(options_count)
                    {
                        case 0:
                        {
                            memcpy(action_a, p->value, p->value_size);
                            action_a[p->value_size] = 0;
                            select_a.base.flags |= GUI_FLAG_DIRTY;
                            zxgui_scene_add(&scene, &select_a);
                            break;
                        }
                        case 1:
                        {
                            memcpy(action_b, p->value, p->value_size);
                            action_b[p->value_size] = 0;
                            select_b.base.flags |= GUI_FLAG_DIRTY;
                            zxgui_scene_add(&scene, &select_b);
                            break;
                        }
                    }

                    options_count++;
                }
            }

            prop++;
        }
    }
    else
    {
        if (index == 1)
        {
            options_to_select.selection = 0;
            options_to_select.last_selection = 0;
            options_to_select.buffer_offset = OPTIONS_TO_SELECT_CAPACITY * sizeof(struct gui_select_option_t*);
            options_to_select.options_size = 0;
            options_to_select.base.flags |= GUI_FLAG_DIRTY;

            if (flags & FLAG_MESSAGE_TO_SIDE)
            {
                options_to_select.base.basics.w = 11;
                options_to_select.base.basics.y = 2;
                options_to_select.base.basics.h = 15;
            }
            else
            {
                options_to_select.base.basics.w = 15;
                options_to_select.base.basics.y = description_offset;
                options_to_select.base.basics.h = 17 - description_offset;
            }

            zxgui_scene_add(&scene, &options_to_select);

            if (has_secondary_options || (flags & FLAG_MESSAGE_TO_SIDE))
            {
                options_to_select.base.basics.x = 0;

                secondary_to_select.selection = 0;
                secondary_to_select.last_selection = 0;
                secondary_to_select.buffer_offset = OPTIONS_TO_SELECT_CAPACITY * sizeof(struct gui_select_option_t*);
                secondary_to_select.options_size = 0;
                secondary_to_select.base.basics.y = description_offset;
                secondary_to_select.base.basics.h = options_to_select.base.basics.h;
                secondary_to_select.base.flags |= GUI_FLAG_DIRTY;

                zxgui_scene_add(&scene, &secondary_to_select);
            }
            else
            {
                options_to_select.base.basics.x = 9;
            }
        }

        ProtoObjectProperty* icon_prop = find_property(object, 'c');

        uint8_t icon_color;
        uint8_t icon_staging[8];

        if (icon_prop)
        {
            if (icon_prop->value_size == 9)
            {
                icon_color = *icon_prop->value;
                memcpy(icon_staging, icon_prop->value + 1, 8);
            }
            else
            {
                uint8_t icon_tile = *icon_prop->value;
                copy_tile_icon_to_staging(icon_tile, icon_staging, &icon_color);
            }
        }
        else
        {
            icon_color = 0;
            memset(icon_staging, 0, sizeof(icon_staging));
        }

        ProtoObjectProperty* o = find_property(object, 'o');

        uint8_t secondary = get_uint8_property(object, 's', 0);

        uint8_t option_index = get_uint8_property(object, 'i', 0);
        uint16_t option_len = o->value_size;
        if (option_len >= sizeof(option_staging))
        {
            option_len = sizeof(option_staging) - 1;
        }
        memcpy(option_staging, o->value, option_len);
        option_staging[option_len] = 0;

        uint8_t* user = zxgui_select_add_option(
            secondary ? &secondary_to_select : &options_to_select,
            option_staging, option_len, 1,
            icon_staging, icon_color);

        if (user)
        {
            if (secondary)
            {
                options_obtain_secondary_data();
            }
            else
            {
                options_obtain_data();
            }
            *user = option_index;
            options_release_data();
        }
        if (option_index == selected_option_index)
        {
            selected_option_found = 1;
            if (secondary)
            {
                secondary_to_select.selection = secondary_to_select.options_size - 1;
                zxgui_scene_set_focus(&scene, &secondary_to_select);
                sync_selected_option(&secondary_to_select);
            }
            else
            {
                options_to_select.selection = options_to_select.options_size - 1;
                zxgui_scene_set_focus(&scene, &options_to_select);
                sync_selected_option(&options_to_select);
            }
        }
    }
}

static void query_complete_callback()
{
    if (!selected_option_found)
    {
        if ((scene.focus == (void*)&secondary_to_select) && secondary_to_select.options_size)
        {
            sync_selected_option(&secondary_to_select);
        }
        else if (options_to_select.options_size)
        {
            zxgui_scene_set_focus(&scene, &options_to_select);
            sync_selected_option(&options_to_select);
        }
        else if (secondary_to_select.options_size)
        {
            zxgui_scene_set_focus(&scene, &secondary_to_select);
            sync_selected_option(&secondary_to_select);
        }
    }

    if (halt_callback_snd)
    {
        halt_callback_snd = 0;
    }
    else
    {
        soundfx(FX_ITEM_3);
    }
    message.base.flags |= GUI_FLAG_DIRTY;

    if (has_edit)
    {
        edit.base.flags |= GUI_FLAG_DIRTY;
        zxgui_scene_add(&scene, &edit);
    }
    else
    {
        btn_exit.base.flags |= GUI_FLAG_DIRTY;
        zxgui_scene_add(&scene, &btn_exit);
    }

    if (has_description)
    {
        description.base.flags |= GUI_FLAG_DIRTY;
        zxgui_scene_add(&scene, &description);
    }
    else
    {
        options_to_select.base.flags |= GUI_FLAG_DIRTY;
        secondary_to_select.base.flags |= GUI_FLAG_DIRTY;
    }
}

static void reset_menus()
{
    btn_exit.base.next = NULL;
    btn_exit.base.flags |= GUI_FLAG_DIRTY;
    description.base.flags |= GUI_FLAG_HIDDEN;
    has_description = 0;
    has_edit = 0;
    flags = 0;
    quick_cancel = 0;
    has_secondary_options = 0;
    selected_option_found = 0;
    strcpy(cancel_action, "Exit");
}

static void switch_query_forced()
{
    reset_menus();
    selected_option_index = 0;
    halt_callback_snd = 1;

    strcpy(message_title, "...");
    message.base.flags |= GUI_FLAG_DIRTY;

    options_to_select.base.flags = 0;
    secondary_to_select.base.flags = 0;
}

static void query_close_if_active()
{
    if (current_scene == &scene)
    {
        panel_close();
    }
}

void module_action(ProtoObject* object) __z88dk_fastcall
{
    switch (get_uint8_property(object, 't', 0))
    {
        case QUERY_MODULE_BEGIN:
        {
            module_object_index = 0;
            zxgui_clear();
            switch_query_forced();
            query_object_callback(module_object_index, object);
            soundfx(FX_ITEM_6);
            break;
        }
        case QUERY_MODULE_OPTION:
        {
            query_object_callback(++module_object_index, object);
            break;
        }
        case QUERY_MODULE_COMPLETE:
        {
            query_complete_callback();
            break;
        }
        case QUERY_MODULE_CLOSE:
        {
            query_close_if_active();
            break;
        }
    }
}

static void select_action(const char* action)
{
    reset_menus();

    declare_str_property_on_stack(req_id, OBJ_PROPERTY_ID, MSG_QUERY_OPTION, NULL);
    declare_str_property_on_stack(action_, 'a', action, &req_id);
    declare_arg_property_on_stack(option, 'o', selected_option_index, &action_);
    declare_object_on_stack(request, 128, &option);

    selected_option_index = 0;

    proto_send_one_nf(request);
}

static void cb_exit()
{
    if (quick_cancel)
    {
        declare_str_property_on_stack(req_id, OBJ_PROPERTY_ID, MSG_QUERY_OPTION, NULL);
        declare_object_on_stack(request, 128, &req_id);

        proto_send_one_nf(request);
        soundfx(FX_ITEM_4);
        panel_close();
    }
    else
    {
        select_action("");
    }
}

void module_loop()
{
}

void module_interrupt()
{
}
