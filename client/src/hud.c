#include "hud.h"
#include "client_graphics.h"
#include "math_utils.h"
#include "client.h"
#include "messages.h"

static uint8_t hud_enabled = 0;
uint8_t hud_dirty = 0;
static uint8_t hud_rendered = 0;
static uint8_t hud_target_marker = 30;
static uint8_t last_hud_target_marker = 30;
static uint8_t hud_target_marker_speed1 = 0;
static uint8_t hud_target_marker_speed2 = 1;

static union {
    struct {
        uint8_t x;
        uint8_t y;
    };
    uint16_t xy;
} hud_target_marker_x;

extern void init_hud()
{
    hud_enabled = 0;
}

void update_target_marker()
{
    if (my_player_object == NULL)
        return;

    if (my_player_object->object.state_flags & STATE_FLAG_AIM)
    {
        enable_target_marker();
    }
    else
    {
        disable_target_marker();
    }
}

void enable_target_marker()
{
    hud_enabled = 1;
}

void disable_target_marker()
{
    hud_enabled = 0;
}

extern void show_hud()
{
    if (my_player_object == 0)
        return;

    hud_rendered = 1;
    render_x(hud_target_marker_x.xy);
}

extern void hide_hud()
{
    if (hud_rendered == 0)
        return;

    hud_rendered = 0;
    render_x(hud_target_marker_x.xy);
}

uint16_t get_target_angle()
{
    if (my_player_object == NULL)
        return 0;

    if (my_player_object->object.f0 & OBJECT_F0_LOOKING_LEFT)
    {
        return 360 - hud_target_marker * 3;
    }
    else
    {
        return hud_target_marker * 3;
    }
}

void clear_hud()
{
    hud_rendered = 0;
}

static void bump_speed()
{
    hud_target_marker_speed1++;
    if (hud_target_marker_speed1 >= 16)
    {
        hud_target_marker_speed1 = 0;
        hud_target_marker_speed2++;
    }
}

static void check_target_sync()
{
    hud_dirty = 1;

    uint8_t x1 = hud_target_marker / 5;
    uint8_t x2 = last_hud_target_marker / 5;

    if (x1 == x2)
        return;

    uint16_t a_angle = (60 - hud_target_marker) * 3;

    declare_str_property_on_stack(req_id, OBJ_PROPERTY_ID, MSG_AIM, NULL);
    declare_arg_property_on_stack(_angle, 'a', a_angle, &req_id);
    declare_object_on_stack(request, 32, &_angle);

    proto_send_one_nf(request);

    last_hud_target_marker = hud_target_marker;
}

void target_marker_cw()
{
    bump_speed();

    hud_target_marker += hud_target_marker_speed2;

    if (hud_target_marker >= 60)
    {
        hud_target_marker = 59;
    }

    check_target_sync();
}

void target_marker_ccw()
{
    bump_speed();

    if (hud_target_marker >= hud_target_marker_speed2)
    {
        hud_target_marker -= hud_target_marker_speed2;
    }

    check_target_sync();
}

void target_marker_stop()
{
    hud_target_marker_speed1 = 0;
    hud_target_marker_speed2 = 1;
}

extern void render_hud()
{
    if (my_player_object == NULL)
        return;

    if (hud_enabled == 0)
    {
        if (hud_rendered)
        {
            hide_hud();
        }
        return;
    }

    if (hud_rendered)
    {
        if (hud_dirty)
        {
            hud_dirty = 0;
        }
        else
        {
            return;
        }

        hide_hud();
    }

    // no hud while moving / flying
    if ((my_player_object->object.f1 & OBJECT_F1_BOTTOM) == 0)
    {
        hud_dirty = 1;
        hide_hud();
        return;
    }

    hud_target_marker_x.x = my_player_object->last_local_coords.x * 8;
    hud_target_marker_x.x += my_player_object->last_local_offset.x;
    hud_target_marker_x.x += 8;
    hud_target_marker_x.y = my_player_object->last_local_coords.y * 8;
    hud_target_marker_x.y += my_player_object->last_local_offset.y;
    hud_target_marker_x.y += 8;

    if (my_player_object->object.f0 & OBJECT_F0_LOOKING_LEFT)
    {
        const uint8_t angle = 60 - hud_target_marker;

        hud_target_marker_x.x -= sin_table[angle]; // -32 .. 32
        hud_target_marker_x.y -= cos_table[angle]; // -32 .. 32
    }
    else
    {
        hud_target_marker_x.x += sin_table[hud_target_marker]; // -32 .. 32
        hud_target_marker_x.y += cos_table[hud_target_marker]; // -32 .. 32
    }

    show_hud();

    hud_rendered = 1;
}
