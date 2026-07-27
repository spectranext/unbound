#include <input.h>
#include <spectrum.h>
#include "spectranet.h"
#include "sys/socket.h"

#include "state.h"
#include "client.h"
#include "proto.h"
#include "proto_req.h"
#include "version.h"
#include "client_object.h"
#include "key_controls.h"
#include "scenes.h"
#include "zxgui.h"
#include "client_graphics.h"
#include "system.h"
#include "messages.h"
#include "notifications.h"
#include "soundfx.h"
#include "text_ui.h"
#include "client_data.h"
#include "frame.h"
#include "modules.h"
#include "vt_sound.h"
#include "particles.h"
#include "hud.h"

enum control_mode_t control_mode = CONTROL_MODE_MOVE;
static uint8_t control_latch = 0;
static int8_t block_selection_x = 2;
static int8_t block_selection_y = 1;
static uint8_t blinking_timer = 0;
static uint8_t selection_wait_release = 0;
static uint8_t touch_scheduled = 0;

uint8_t my_health = 100;
uint8_t my_temperature = 100;
uint8_t my_power = 100;
uint16_t my_credits = 0;

uint8_t my_hit_auto = 0;
uint8_t my_hit_delay = 0;
uint8_t my_stats_dirty = 0;
uint8_t construction_mode_dirty = 0;
uint8_t hit_delay = 0;
uint8_t hit_latch = 0;
uint8_t got_tiles = 0;
static uint8_t i_;
char my_building_state[20] = {};

static uint8_t* get_selection_attr()
{
    if (camera_x == 0xFF)
        return NULL;

    uint8_t pos_x = (uint16_t)(OBJECT_PHY_TO_LOGICAL(my_player_object->object.location.x) + block_selection_x) - camera_base_x;
    uint8_t pos_y = (uint16_t)(OBJECT_PHY_TO_LOGICAL(my_player_object->object.location.y) + block_selection_y) - camera_base_y;

    return zx_cxy2aaddr(pos_x, pos_y);
}

static void restore_original_selection_color()
{
    client_map_get_block_reset_cache();

    if (my_player_object == NULL)
        return;

    block_t block = client_map_get_block(
        OBJECT_PHY_TO_LOGICAL(my_player_object->object.location.x) + block_selection_x,
        OBJECT_PHY_TO_LOGICAL(my_player_object->object.location.y) + block_selection_y);

    switch_tile_data_a();
    *get_selection_attr() = tiles_a[strip_block_flags(block) + 2048];
    get_objects_a();
}

static void set_original_selection_color()
{
    blinking_timer = 0;
}

static void move_up()
{
    struct map_object_t* o = &my_player_object->object;

    if (IS_F0_SET(o, OBJECT_F0_FIXING))
    {
        o->speed.y = -1;
    }
    else
    {
        // boost up jumping to full OBJECT_JUMP_MAXIMUM, until
        // we reach full speed or stop

        if (my_player_object->jumping)
        {
            if (unique_frame)
            {
                if (o->speed.y > OBJECT_JUMP_MAXIMUM)
                {
                    my_player_object->jumping++;

                    if (my_player_object->jumping >= 8)
                    {
                        my_player_object->jumping = 1;
                        o->speed.y -= 1;
                    }
                }
                else
                {
                    my_player_object->jumping = 0;
                }
            }
        }
        else
        {
            if (IS_F1_SET(o, OBJECT_F1_BOTTOM) && (o->speed.y == 0))
            {
                o->speed.y = OBJECT_JUMP_MINIMUM;
                my_player_object->jumping = 1;
            }
        }
    }
}

static void update_move_controls()
{
    if (my_player_object == NULL)
        return;

    struct map_object_t* o = &my_player_object->object;

    static uint8_t move_counter = 0;
    static uint8_t move_tick = 0;

    // trigger once per 16 unique frames

    if (unique_frame)
    {
        move_counter++;
        if (move_counter > OBJECT_HORIZONTAL_SPEED_TICK_RATE)
        {
            move_counter = 0;
            move_tick = 1;
        }
        else
        {
            move_tick = 0;
        }
    }
    else
    {
        move_tick = 0;
    }

    static uint8_t row;
    static uint8_t test_other_actions;
    test_other_actions = 1;

    row = poll_key_row(ROW_POIUY);

    if ((row & KEY_BIT_O) == 0)
    {
        if ((o->f1 & OBJECT_F1_LEFT) == 0)
        {
            o->speed.x = -1;
        }
        /*
        if (o->f1 & OBJECT_F1_BOTTOM)
        {
            o->speed.x = -1;
        }
        else
        {
            if (o->speed.x == 0)
            {
                o->speed.x = -1;
                move_counter = 0;
            }
            else if (move_tick)
            {
                if (o->speed.x > -OBJECT_MAX_HORIZONTAL_SPEED)
                {
                    o->speed.x -= 1;
                }
            }
        }
        */
        test_other_actions = 0;
    }
    else if ((row & KEY_BIT_P) == 0)
    {
        if ((o->f1 & OBJECT_F1_RIGHT) == 0)
        {
            o->speed.x = 1;
        }
        /*
        if (o->f1 & OBJECT_F1_BOTTOM)
        {
            o->speed.x = 1;
        }
        else
        {
            if (o->speed.x == 0)
            {
                o->speed.x = 1;
                move_counter = 0;
            }
            else if (move_tick)
            {
                if (o->speed.x < OBJECT_MAX_HORIZONTAL_SPEED)
                {
                    o->speed.x += 1;
                }
            }
        }
        */
        test_other_actions = 0;
    }
    else
    {
        o->speed.x = 0;
    }

    if ((row & KEY_BIT_I) == 0)
    {
        switch_query("inventory");
        return;
    }

    row = poll_key_row(ROW_QWERT);
    static uint8_t row2;
    row2 = poll_key_row(ROW_ASDFG);

    if (unique_frame)
    {
        if ((row2 & KEY_BIT_S) == 0)
        {
            target_marker_ccw();
        }
        else if ((row & KEY_BIT_W) == 0)
        {
            target_marker_cw();
        }
        else
        {
            target_marker_stop();
        }
    }

    if ((row & KEY_BIT_T) == 0)
    {
        // soundfx(FX_ITEM_6);
        switch_terminal();
        return;
    }

    if ((row & KEY_BIT_E) == 0)
    {
        switch_query("status");
        return;
    }

    if ((row & KEY_BIT_Q) == 0)
    {
        move_up();
        test_other_actions = 0;
    }
    else
    {
        // reset jumping
        my_player_object->jumping = 0;

        if (my_player_object->object.f0 & OBJECT_F0_FIXING)
        {
            if (((row2 & KEY_BIT_A) == 0) && ((my_player_object->object.f1 & OBJECT_F1_BOTTOM) == 0))
            {
                my_player_object->object.speed.y = 1;
            }
            else
            {
                my_player_object->object.speed.y = 0;
            }
        }
    }

    if (test_other_actions == 0)
        return;

    if (my_player_object->object.speed.y)
        return;

    row = poll_key_row(ROW_SPACE_SYM_MNB);

    if (unique_frame)
    {
        if (hit_delay)
        {
            hit_delay--;
        }
        else
        {
            if (hit_latch == 0)
            {
                if (row & KEY_BIT_SPACE)
                {
                    // reset hit ability (one-shot)
                    hit_latch = 1;
                }
            }
            else if ((row & KEY_BIT_SPACE) == 0)
            {
                uint16_t angle = get_target_angle();

                declare_str_property_on_stack(req_id, OBJ_PROPERTY_ID, MSG_HIT, NULL);
                declare_arg_property_on_stack(_angle, 'a', angle, &req_id);
                declare_object_on_stack(request, 32, &_angle);

                proto_send_one_nf(request);

                hit_latch = my_hit_auto;
                hit_delay = my_hit_delay;

                return;
            }
        }
    }

    row = poll_key_row(ROW_SHIFT_ZXCV);

    if ((row & KEY_BIT_C) == 0)
    {
        // soundfx(FX_ITEM_6);
        panel_open();
        switch_chat();
        return;
    }

    if (control_latch)
    {
        // released
        if (row & KEY_BIT_SHIFT)
        {
            control_latch = 0;
            return;
        }
    }
    else
    {
        // pressed
        if ((row & KEY_BIT_SHIFT) == 0)
        {

            construction_mode_dirty = 1;
            set_original_selection_color();
            control_mode = CONTROL_MODE_SELECT_BLOCK;
            my_stats_dirty = 1;
            disable_target_marker();
            return;
        }
    }
}

static void touch_object_callback(uint8_t index, ProtoObject* object)
{
    touch_scheduled = get_uint8_property(object, 'p', 0);
}

static void touch_complete_callback(struct proto_process_t* proto)
{
    if (touch_scheduled)
    {
        control_mode = CONTROL_MODE_TOUCH_SCHEDULE;
        clear_notification_progress();
    }
    else
    {
        control_mode = CONTROL_MODE_SELECT_BLOCK;
        disable_target_marker();
    }

    selection_wait_release = 1;
}

static void touch_error_callback(const char* error)
{
    control_mode = CONTROL_MODE_SELECT_BLOCK;
    my_stats_dirty = 1;
    disable_target_marker();
    selection_wait_release = 1;
    show_notification(error, strlen(error), INK_RED | BRIGHT | PAPER_BLACK);
}

void clear_scheduled_touch()
{
    control_mode = CONTROL_MODE_SELECT_BLOCK;
    my_stats_dirty = 1;
    disable_target_marker();
    selection_wait_release = 1;
}

extern uint8_t last_notification_progress;

void render_my_stats()
{
    if (my_player_object == NULL)
        return;

    if (last_notification_progress != 0xFF)
        return;

    if (animation & 0b111)
        return;

    my_stats_dirty = 0;

    if (panel)
        return;

    static char b[4];

    screen_color = INK_BLACK | PAPER_BLACK;
    zxgui_screen_clear(0, 0, 9, 1);

    static uint8_t icon;

    {
        if (my_health >= 75)
        {
            icon = HEALTH_4;
            screen_color = INK_GREEN | BRIGHT | PAPER_BLACK;
        }
        else if (my_health >= 50)
        {
            icon = HEALTH_3;
            screen_color = INK_YELLOW | BRIGHT | PAPER_BLACK;
        }
        else if (my_health >= 25)
        {
            icon = HEALTH_2;
            screen_color = INK_RED | BRIGHT | PAPER_BLACK;
        }
        else
        {
            my_stats_dirty = 1;
            icon = HEALTH_1;

            if (animation & 8)
            {
                screen_color = INK_BLACK | BRIGHT | PAPER_BLACK;
            }
            else
            {
                screen_color = INK_RED | BRIGHT | PAPER_BLACK;
            }
        }

        zxgui_screen_put(0, 0, icon);

        text_ui_color(screen_color);
        itoa(my_health, b, 10);
        text_ui_puts_at(1, 0, b);
    }

    {
        if (my_temperature < 25)
        {
            my_stats_dirty = 1;

            if (animation & 8)
            {
                screen_color = INK_BLACK | BRIGHT | PAPER_BLACK;
            }
            else
            {
                screen_color = INK_RED | BRIGHT | PAPER_BLACK;
            }
        }
        else
        {
            screen_color = INK_RED | BRIGHT | PAPER_BLACK;
        }

        zxgui_screen_put(3, 0, TEMPERATURE);

        text_ui_color(screen_color);
        itoa(my_temperature, b, 10);
        text_ui_puts_at(4, 0, b);
    }

    {
        if (my_power >= 75)
        {
            icon = POWER_4;
            screen_color = INK_CYAN | BRIGHT | PAPER_BLACK;
        }
        else if (my_power >= 50)
        {
            icon = POWER_3;
            screen_color = INK_CYAN | PAPER_BLACK;
        }
        else if (my_power >= 25)
        {
            icon = POWER_2;
            screen_color = INK_YELLOW | BRIGHT | PAPER_BLACK;
        }
        else
        {
            my_stats_dirty = 1;
            icon = POWER_1;

            if (animation & 8)
            {
                screen_color = INK_BLACK | BRIGHT | PAPER_BLACK;
            }
            else
            {
                screen_color = INK_RED | BRIGHT | PAPER_BLACK;
            }
        }

        zxgui_screen_put(6, 0, icon);

        text_ui_color(screen_color);
        itoa(my_power, b, 10);
        text_ui_puts_at(7, 0, b);
    }

    {
        icon = CREDITS;
        screen_color = INK_WHITE | PAPER_BLACK;

        zxgui_screen_put(9, 0, icon);
        text_ui_color(screen_color);
        itoa(my_credits, b, 10);
        text_ui_puts_at(10, 0, b);
    }

    const char* s_state;

    if (control_mode != 0)
    {
        s_state = my_building_state;
        screen_color = INK_YELLOW | BRIGHT | PAPER_BLACK;
    }
    else
    {
        s_state = my_default_state;
        screen_color = INK_WHITE | PAPER_BLACK;
    }

    if (*s_state == '!')
    {
        screen_color = INK_RED | BRIGHT | PAPER_BLACK;
        s_state++;
    }

    text_ui_color(screen_color);
    text_ui_puts_at(20, 0, s_state);
}

static void cancel_touching()
{
    clear_scheduled_touch();
    clear_notification_progress();

    declare_str_property_on_stack(req_id, OBJ_PROPERTY_ID, MSG_TOUCH_CANCEL, NULL);
    declare_object_on_stack(request, 128, &req_id);

    proto_send_one_nf(request);
}

static void update_selection_blinking()
{
    uint8_t* attr = get_selection_attr();

    if (blinking_timer & 0x08)
    {
        if ((blinking_timer & 0x04) == 0)
        {
            *attr = INK_WHITE | PAPER_BLACK | BRIGHT;
        }
        else
        {
            *attr = INK_BLACK | PAPER_WHITE | BRIGHT;
        }
    }
    else
    {
        if ((blinking_timer & 0x04) == 0)
        {
            *attr = INK_YELLOW | PAPER_BLACK | BRIGHT;
        }
        else
        {
            *attr = INK_BLACK | PAPER_YELLOW | BRIGHT;
        }
    }

    if (unique_frame)
    {
        blinking_timer++;
    }
}

static void cancel_selection_controls()
{
    restore_original_selection_color();
    control_mode = CONTROL_MODE_MOVE;
    my_stats_dirty = 1;
    update_target_marker();
}

static void update_touch_controls()
{
    if (my_player_object == NULL)
        return;

    update_selection_blinking();

    uint8_t row_1 = poll_key_row(ROW_ENTER_LKJH);

    if (selection_wait_release)
    {
        // released
        if (row_1 & KEY_BIT_ENTER)
        {
            selection_wait_release = 0;
        }
        else
        {
            return;
        }
    }

    // pressed again
    if ((row_1 & KEY_BIT_ENTER) == 0)
    {
        cancel_touching();
    }

    uint8_t row_5 = poll_key_row(ROW_SHIFT_ZXCV);

    // pressed again
    if ((row_5 & KEY_BIT_SHIFT) == 0)
    {
        cancel_touching();
        cancel_selection_controls();
    }
}

static void update_selection_controls()
{
    if (my_player_object == NULL)
        return;

    if (my_player_object->object.speed.x || my_player_object->object.speed.y)
    {
        restore_original_selection_color();
        return;
    }

    if (construction_mode_dirty)
    {
        construction_mode_dirty = 0;

        screen_color = PAPER_BLACK | INK_YELLOW | BRIGHT;

        i_ = 32;
        do
        {
            i_--;
            zxgui_screen_put(i_, 23, 21);
        }
        while (i_);

        text_ui_color(PAPER_BLACK | INK_YELLOW | BRIGHT);
        text_ui_puts_at(11, 23, " CONSTRUCTION  MODE ");
    }

    uint8_t touching = (control_mode == CONTROL_MODE_TOUCH_SCHEDULE);

    uint8_t row_1 = poll_key_row(ROW_ENTER_LKJH);
    uint8_t row_2 = poll_key_row(ROW_QWERT);
    uint8_t row_3 = poll_key_row(ROW_ASDFG);
    uint8_t row_4 = poll_key_row(ROW_POIUY);
    uint8_t row_5 = poll_key_row(ROW_SHIFT_ZXCV);

    if (control_latch)
    {
        // key was pressed at some point but currently released

        if ((row_5 & KEY_BIT_SHIFT) == 0)
        {
            // pressed again
            if (touching)
            {
                cancel_touching();
            }

            zxgui_screen_color(INK_BLACK | PAPER_BLACK);
            zxgui_screen_recolor(0, 23, 32, 1);

            cancel_selection_controls();
            return;
        }
    }
    else
    {
        if (row_5 & KEY_BIT_SHIFT)
        {
            // released
            control_latch = 1;
        }
    }

    update_selection_blinking();

    if (selection_wait_release)
    {
        if (((row_1 & KEY_BIT_ENTER) == 0) ||
            ((row_2 & KEY_BIT_Q) == 0) ||
            ((row_3 & KEY_BIT_A) == 0) ||
            (((row_4 & (KEY_BIT_P | KEY_BIT_O)) != (KEY_BIT_P | KEY_BIT_O))))
        {
            return;
        }

        if (touching)
        {
            cancel_touching();
        }

        selection_wait_release = 0;
    }

    if ((row_1 & KEY_BIT_ENTER) == 0)
    {
        uint16_t my_x = OBJECT_PHY_TO_LOGICAL(my_player_object->object.location.x);
        uint16_t my_y = OBJECT_PHY_TO_LOGICAL(my_player_object->object.location.y);

        uint16_t pos_x = (uint16_t)(my_x + block_selection_x);
        uint16_t pos_y = (uint16_t)(my_y + block_selection_y);

        declare_str_property_on_stack(req_id, OBJ_PROPERTY_ID, MSG_TOUCH, NULL);
        declare_arg_property_on_stack(_x, 'x', pos_x, &req_id);
        declare_arg_property_on_stack(_y, 'y', pos_y, &_x);
        declare_object_on_stack(request, 128, &_y);

        control_mode = CONTROL_MODE_TOUCH;
        my_stats_dirty = 1;
        touch_scheduled = 0;

        proto_req_send_request(request, touch_object_callback, touch_complete_callback, touch_error_callback);

        selection_wait_release = 1;
        return;
    }

    if ((row_2 & KEY_BIT_Q) == 0)
    {
        if (block_selection_y > -4)
        {
            restore_original_selection_color();
            selection_wait_release = 1;
            block_selection_y--;
            set_original_selection_color();
        }
    }

    if ((row_3 & KEY_BIT_A) == 0)
    {
        if (block_selection_y < 3)
        {
            restore_original_selection_color();
            selection_wait_release = 1;
            block_selection_y++;
            set_original_selection_color();
        }
    }

    if ((row_4 & KEY_BIT_P) == 0)
    {
        if (block_selection_x < 4)
        {
            restore_original_selection_color();
            selection_wait_release = 1;
            block_selection_x++;
            set_original_selection_color();
        }
    }

    if ((row_4 & KEY_BIT_O) == 0)
    {
        if (block_selection_x > -3)
        {
            restore_original_selection_color();
            selection_wait_release = 1;
            block_selection_x--;
            set_original_selection_color();
        }
    }
}

static void update_game_input()
{
    if (rendering_blocked)
        return;

    if (panel)
        return;

    switch (control_mode)
    {
        case CONTROL_MODE_MOVE:
        {
            update_move_controls();
            break;
        }
        case CONTROL_MODE_SELECT_BLOCK:
        {
            update_selection_controls();
            break;
        }
        case CONTROL_MODE_TOUCH:
        case CONTROL_MODE_TOUCH_SCHEDULE:
        {
            update_touch_controls();
        }
        default:
        {
            break;
        }
    }
}

// modified by asm
uint8_t set_unique_frame = 0;
uint8_t round_robin_operation = 0;

static void check_module_loop()
{
    if (module_loop_active != 0xFF)
    {
        module_call_namespace = module_loop_active;
        setpagea(SPECTRANET_MODULES_NAMESPACE0 + module_loop_active);
        module_loop();
        get_objects_a();
    }
}

static void check_gui_scene_iteration()
{
    if (current_scene_module != MODULE_NONE)
    {
        if (current_scene == NULL)
        {
            current_scene_module = MODULE_NONE;
            return;
        }

        module_call_namespace = current_scene_module;
        setpagea(SPECTRANET_MODULES_NAMESPACE0 + current_scene_module);
        zxgui_scene_iteration();
        get_objects_a();
        client_map_get_b();
    }
    else
    {
        zxgui_scene_iteration();
    }
}

static void loop_state_none()
{
    while (1)
    {
        unique_frame = set_unique_frame;
        set_unique_frame = 0;

        if (client_socket)
        {
            proto_client_process(proto_req_new_request,
                proto_req_object_callback, proto_req_recv, &proto_req_processor);

            update_notifications();
        }

        if (unique_frame || current_scene)
        {
            check_gui_scene_iteration();
        }

        if (state_active_phase)
        {
            update_target_marker();
            module_scene_clear();
            zxgui_clear();
            if (my_player_object)
            {
                my_stats_dirty = 1;
            }
            client_map_b.screen_dirty = 1;
            return;
        }
    }
}

uint8_t state_active_phase = 0;

static void loop_active_phase()
{
    while (1)
    {
        unique_frame = set_unique_frame;
        set_unique_frame = 0;

        if (unique_frame)
        {
            check_module_loop();
            // render_particles(); <- rendered by an ISR
            render_hud();

            switch_tile_data_a();
            client_map_get_b();

            check_gui_scene_iteration();

            if (module_music_active)
            {
                if (vt_setup_byte & 0x80)
                {
                    vt_mute();
                    module_music_active = 0;
                    // report music has concluded
                    client_action("music");
                }
            }

            client_map_render();
            client_map_update();

            render_particles();

            if (my_player_object)
            {
                update_game_input();
            }

            update_notifications();
        }

        if (client_socket)
        {
            proto_client_process(proto_req_new_request,
                proto_req_object_callback, proto_req_recv, &proto_req_processor);
        }
    }
}

void game_state_loop()
{
    if (state_active_phase)
    {
        loop_active_phase();
    }
    else
    {
        loop_state_none();
    }
}
