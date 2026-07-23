#include "object.h"

#ifdef __CLION_IDE__
#define SPECTRUM
#endif

#ifdef SPECTRUM
#include "client_object.h"
#include "client.h"

#define check_is_block_blocking(x, y) \
    is_block_blocking(client_map_get_block(x, y))

#define check_is_block_fixing(x, y) \
    is_block_fixing(client_map_get_block(x, y))

#else

#include "server.h"
#include "server_map.h"

#define check_is_block_blocking(x, y) \
    (map_get_block(&get_server_state()->map.map, x, y) & 0x8000)

#define check_is_block_fixing(x, y) \
    (map_get_block(&get_server_state()->map.map, x, y) & 0x4000)

#include <stdio.h>
#endif

void init_object(struct map_object_t* o)
{
    update_object_boundaries(o);
}

void update_object_sprite(struct map_object_t* o, int8_t speed_x, uint8_t animation_move, uint8_t animation_pick)
{
    if (o->type & MAP_OBJECT_SPRITE)
    {
        return;
    }

    if ((o->f1 & OBJECT_F1_BOTTOM) == 0)
    {
        o->sprite_data_id = o->data_move_id;

        if (speed_x)
        {
            if (speed_x > 0)
            {
                o->sprite_offset = OBJECT_SP_OFFSET_FLYING_RIGHT;
                RESET_F0(o, OBJECT_F0_LOOKING_LEFT);
            }
            else
            {
                o->sprite_offset = OBJECT_SP_OFFSET_FLYING_LEFT;
                SET_F0(o, OBJECT_F0_LOOKING_LEFT);
            }
        }
        else if (IS_F0_SET(o, OBJECT_F0_LOOKING_LEFT))
        {
            o->sprite_offset = OBJECT_SP_OFFSET_FLYING_LEFT;
        }
        else
        {
            o->sprite_offset = OBJECT_SP_OFFSET_FLYING_RIGHT;
        }

        set_object_dirty(o);
        return;
    }

    if (speed_x)
    {
        o->sprite_data_id = o->data_move_id;

        if (speed_x < 0)
        {
            o->sprite_offset = OBJECT_SP_OFFSET_MOVING_LEFT + animation_move;
            SET_F0(o, OBJECT_F0_LOOKING_LEFT);
        }
        else
        {
            o->sprite_offset = OBJECT_SP_OFFSET_MOVING_RIGHT + animation_move;
            RESET_F0(o, OBJECT_F0_LOOKING_LEFT);
        }

        return;
    }

    static uint16_t off;

    if (o->state == OBJECT_STATE_PICKING)
    {
        off = animation_pick;

        if (!IS_F0_SET(o, OBJECT_F0_LOOKING_LEFT))
        {
            off += OBJECT_SP_OFFSET_PICKING_RIGHT;
        }

        if (off != o->sprite_offset || (o->sprite_data_id != o->data_picking_id))
        {
            o->sprite_data_id = o->data_picking_id;
            o->sprite_offset = off;
            set_object_dirty(o);
        }
    }
    else
    {
        off = o->state;
        off *= OBJECT_SP_OFFSET;
        if (IS_F0_SET(o, OBJECT_F0_LOOKING_LEFT) == 0)
        {
            off += OBJECT_SP_OFFSET_RIGHT;
        }

        if (off != o->sprite_offset || (o->sprite_data_id != o->data_id))
        {
            o->sprite_data_id = o->data_id;
            o->sprite_offset = off;
            set_object_dirty(o);
        }
    }
}

void update_object_boundaries(struct map_object_t* o)
{
    static uint16_t xx;
    static uint16_t yy;

    xx = OBJECT_PHY_TO_LOGICAL(o->location.x);
    yy = OBJECT_PHY_TO_LOGICAL(o->location.y);

    if (xx == o->flags_at.x && yy == o->flags_at.y)
    {
        return;
    }

    o->flags_at.x = xx;
    o->flags_at.y = yy;

    static uint8_t f0;
    static uint8_t f1;

    f0 = o->f0;

#ifdef SPECTRUM
    client_map_get_block_reset_cache();
#endif

    /*
     * 4x4 collision view (2x2 player + 1 tile margin around):
     *
     *             xx-1  xx     xx+1   xx+2
     * yy-2       [  ]  [ T ]  [ T ]  [   ]
     * yy-1       [ L ] [ * ]  [ * ]  [ R ]
     * yy         [ L ] [ # ]  [ * ]  [ R ]
     * yy+1       [  ]  [ B ]  [ B ]  [   ]
     *
     * * = player body tiles (2x2)
     * # - player coordinates block
     * L/R/T/B = wall probes for LEFT/RIGHT/TOP/BOTTOM flags
     *
     */

    f1 = 0;

    SET_COND_F(f1, check_is_block_blocking(xx, yy), OBJECT_F1_LEFT);
    SET_COND_F(f1, check_is_block_blocking(xx, yy - 1), OBJECT_F1_LEFT);

    xx += 2;

    SET_COND_F(f1, check_is_block_blocking(xx, yy), OBJECT_F1_RIGHT);
    SET_COND_F(f1, check_is_block_blocking(xx, yy - 1), OBJECT_F1_RIGHT);

    xx -= 2;

    f0 &= ~(OBJECT_F0_FIXING);

    SET_COND_F(f0, check_is_block_fixing(xx, yy), OBJECT_F0_FIXING);
    SET_COND_F(f0, check_is_block_fixing(xx + 1, yy), OBJECT_F0_FIXING);
    SET_COND_F(f0, check_is_block_fixing(xx, yy - 1), OBJECT_F0_FIXING);
    SET_COND_F(f0, check_is_block_fixing(xx + 1, yy - 1), OBJECT_F0_FIXING);

    yy++;

    SET_COND_F(f1, check_is_block_blocking(xx++, yy), OBJECT_F1_BOTTOM);
    SET_COND_F(f1, check_is_block_blocking(xx++, yy), OBJECT_F1_BOTTOM);

    xx -= 2;
    yy -= 2;

    SET_COND_F(f1, check_is_block_blocking(xx++, yy), OBJECT_F1_TOP);
    SET_COND_F(f1, check_is_block_blocking(xx++, yy), OBJECT_F1_TOP);

#ifdef SPECTRUM
    get_objects_a();
#endif

    o->f0 = f0;
    o->f1 = f1;
}

void update_object(struct map_object_t* o)
{
    const uint16_t old_x = o->location.x;
    const uint16_t old_y = o->location.y;

#ifndef SPECTRUM
    if ((o->type & MAP_OBJECT_STATIC) == 0)
#endif
    {
        update_object_boundaries(o);
    }

    static int8_t speed_x;
#ifdef SPECTRUM
    speed_x = o->speed.x;
#else
    speed_x = o->speed.x + o->adjustment_speed.x;
#endif

    if (speed_x)
    {
        {
            if (speed_x < 0)
            {
                if (o->f1 & OBJECT_F1_LEFT)
                {
                    if (o->location.x & 0x07)
                    {
                        // drop the offset and move over
                        o->location.x &= ~(0x07);
                        o->location.x += 8;
                    }

                    o->flags_at.x = 0;
                    update_object_boundaries(o);
#ifndef SPECTRUM
                    if (o->type & MAP_OBJECT_COLLIDER)
                    {
                        SET_F0(o, OBJECT_F0_COLLIDED);
                    }
#endif
                    o->speed.x = 0;
                }
            }
            else
            {
                if (o->f1 & OBJECT_F1_RIGHT)
                {
                    // drop the offset
                    o->location.x &= ~(0x07);

                    o->flags_at.x = 0;
                    update_object_boundaries(o);

#ifndef SPECTRUM
                    if (o->type & MAP_OBJECT_COLLIDER)
                    {
                        SET_F0(o, OBJECT_F0_COLLIDED);
                    }
#endif
                    o->speed.x = 0;
                }
            }
        }

    }

#ifndef SPECTRUM
    if ((o->type & MAP_OBJECT_STATIC) == 0)
#endif
    {
        if (((o->type & MAP_OBJECT_STATIC) == 0) && ((o->f0 & OBJECT_F0_FIXING) == 0))
        {
            if (IS_F1_SET(o, OBJECT_F1_TOP))
            {
                if (o->speed.y < 0)
                {
                    // drop the offset and move over
                    if (o->location.y & 0x07)
                    {
                        o->location.y &= ~(0x07);
                        o->location.y += 8;
                    }
                    o->speed.y = 0;
                }
            }

            if (IS_F1_SET(o, OBJECT_F1_BOTTOM))
            {
                if (o->speed.y > 0)
                {
#ifndef SPECTRUM
                    o->landed_after_fall = 1;
                    o->fall_speed = o->speed.y;

                    if (o->type & MAP_OBJECT_COLLIDER)
                    {
                        SET_F0(o, OBJECT_F0_COLLIDED);
                    }
#endif
                    // drop the offset
                    o->location.y &= ~0x07;
                    o->gravity = 0;
                    o->speed.y = 0;

                    o->flags_at.y = 0;
                }
            }
            else
            {
                o->gravity++;
                if (o->gravity > STATE_GRAVITY_TICKS)
                {
                    o->gravity = 1;
                    if (o->speed.y < STATE_MAX_GRAVITY)
                    {
                        o->speed.y += 1;
                    }
                }
            }
        }
    }

#ifdef SPECTRUM
    o->location.x = o->location.x + o->speed.x;
    o->location.y = o->location.y + o->speed.y;
#else
    o->location.x = o->location.x + o->speed.x + o->adjustment_speed.x;
    o->location.y = o->location.y + o->speed.y + o->adjustment_speed.y;
#endif

    if (o->location.x != old_x || o->location.y != old_y)
    {
        set_object_dirty(o);
    }

#ifdef SPECTRUM
    update_object_sprite(o, speed_x, animation_move, animation_pick);
#endif
}
