#ifndef __OBJECT_H
#define __OBJECT_H

#include <stdint.h>

typedef uint8_t map_object_type_t;

#define MAP_OBJECT_PLAYER       0x01
#define MAP_OBJECT_STATIC       0x02
#define MAP_OBJECT_SLOW         0x04
#define MAP_OBJECT_COLLIDER     0x08
#define MAP_OBJECT_SPRITE       0x10

#define JUMP_POTENTIAL  (4)

enum client_object_state_t
{
    OBJECT_STATE_NOTHING = 0,
    OBJECT_STATE_CONTROL,
    OBJECT_STATE_PICKING,
};

#define OBJECT_TARGET_FORCE_SYNC     (64)
#define OBJECT_TARGET_ADJUST         (4)

#define STATE_FLAG_FLASHING          (0x01)
#define STATE_FLAG_AIM               (0x02)

#define OBJECT_F0_USED               (0x01)
#define OBJECT_F0_LOOKING_LEFT       (0x02)
#define OBJECT_F0_DIRTY              (0x04)
#define OBJECT_F0_COLLIDED           (0x10)
#define OBJECT_F0_HIDDEN             (0x20)
#define OBJECT_F0_FIXING             (0x40)

#define OBJECT_F1_BOTTOM             (0x01)
#define OBJECT_F1_LEFT               (0x02)
#define OBJECT_F1_RIGHT              (0x04)
#define OBJECT_F1_TOP                (0x08)

#define STATE_GRAVITY_TICKS          8
#define STATE_MAX_GRAVITY            6
#define OBJECT_JUMP_MAXIMUM          (-3)
#define OBJECT_JUMP_MINIMUM          (-2)
#define OBJECT_MAX_HORIZONTAL_SPEED  2
#define OBJECT_HORIZONTAL_SPEED_TICK_RATE 8
#define OBJECT_PREDICTION_FRAMES        (24)

#define OBJECT_SP_OFFSET                (64)
#define OBJECT_SP_OFFSET_RIGHT          (32)
#define OBJECT_SP_OFFSET_CONTROL_LEFT   (64)
#define OBJECT_SP_OFFSET_CONTROL_RIGHT  (96)
#define OBJECT_SP_OFFSET_EXT1_LEFT      (128)
#define OBJECT_SP_OFFSET_EXT1_RIGHT     (160)
#define OBJECT_SP_OFFSET_PICKING_RIGHT  (128)

#define OBJECT_SP_OFFSET_FLYING_LEFT    (0)
#define OBJECT_SP_OFFSET_FLYING_RIGHT   (24)
#define OBJECT_SP_OFFSET_MOVING_LEFT    (48)
#define OBJECT_SP_OFFSET_MOVING_RIGHT   (144)

#define OBJECT_LOGICAL_TO_PHY(value) ((value) << 3)
#define OBJECT_PHY_TO_LOGICAL(value) ((value) >> 3)
#define OBJECT_MATCH_LOGICAL(value) (value & 0xFFF8)
#define OBJECT_PHY_TO_LOGICAL_CHUNK(value) ((value) >> 6)

#define SET_COND_F(o, cond, flag) if (cond) o |= flag

#define IS_F0_SET(o, flag) (o->f0 & flag)
#define SET_F0(o, flag) o->f0 |= flag
#define RESET_F0(o, flag) o->f0 &= ~(flag)

#define IS_F1_SET(o, flag) (o->f1 & flag)
#define SET_F1(o, flag) o->f1 |= flag
#define SET_COND_F1(o, cond, flag) if (cond) o->f1 |= flag
#define RESET_F1(o, flag) o->f1 &= ~(flag)

#ifdef SPECTRUM
struct map_object_location_t
#else
struct __attribute__((__packed__)) map_object_location_t
#endif
{
    uint16_t x;
    uint16_t y;
};

#ifdef SPECTRUM
struct map_object_speed_t
#else
struct __attribute__((__packed__)) map_object_speed_t
#endif
{
    int8_t x;
    int8_t y;
};

#ifdef SPECTRUM
struct object_prediction_t
#else
struct __attribute__((__packed__)) object_prediction_t
#endif
{
    uint16_t x;
    uint16_t y;
    uint8_t sprite_data_id;
    uint16_t sprite_offset;
};

#ifdef SPECTRUM
struct map_object_t
#else
struct __attribute__((__packed__)) map_object_t
#endif
{
#ifndef SPECTRUM
    struct {
        int8_t x;
        int8_t y;
    } adjustment_speed;

    struct {
        uint16_t x;
        uint16_t y;
    } target;

    uint8_t landed_after_fall;
    int8_t fall_speed;
#endif

    uint8_t state_flags;

    /*
     *      [l1][  ][  ][r1]
     *      [l0][  ][  ][r0]
     *  [m2][m1][p0][p0][p0][p3]
     */

    struct {
        uint16_t x;
        uint16_t y;
    } flags_at;

    uint8_t f0;
    uint8_t f1;

    enum client_object_state_t state;

    /* SYNC CRITICAL */

    uint16_t object_id;
    map_object_type_t type;
    uint8_t data_id;
    uint8_t data_move_id;
    uint8_t data_picking_id;
    uint8_t team_id;

    union
    {
        uint16_t payload;
        uint16_t client_id;
    };

    /* MOVE CRITICAL */

    struct map_object_location_t location;

    /* > 4 bytes */

    struct map_object_speed_t speed;

    /* > 6 bytes */

    uint8_t gravity;

    /* > 7 bytes */

    uint16_t sprite_offset;
    uint8_t sprite_data_id;

    /* > 9 bytes */
};

#define MAP_OBJECT_SYNC_CRITICAL_SIZE 16
#define MAP_OBJECT_SYNC_CRITICAL_SIZE_W_SPRITE_OFFSET 19

#define MAP_OBJECT_MOVE_CRITICAL_SIZE 7
#define MAP_OBJECT_MOVE_CRITICAL_SIZE_W_SPRITE_OFFSET 10

enum object_aninamtion_t
{
    OBJECT_ANIMATION_NOTHING = 0,
    OBJECT_ANIMATION_MOVING_RIGHT,
    OBJECT_ANIMATION_MOVING_LEFT,
    OBJECT_ANIMATION_FLYING,
};

#ifdef SPECTRUM
#define set_object_dirty(o) \
    SET_F0(o, OBJECT_F0_DIRTY);
#else
extern void set_object_dirty(struct map_object_t* o);
#endif

extern void init_object(struct map_object_t* o);
extern void update_object_boundaries(struct map_object_t* o);
extern void update_object(struct map_object_t* o);
extern void update_object_sprite(struct map_object_t* o, int8_t speed_x, uint8_t animation_move, uint8_t animation_pick);

#endif
