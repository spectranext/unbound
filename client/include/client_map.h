#ifndef __CLIENT_MAP_H
#define __CLIENT_MAP_H

#include <stdint.h>
#include "client_object.h"
#include "map.h"
#include "chat.h"

#define MAX_CHUNKS_TO_CACHE (128)
#define MAX_CHUNKS_TO_CACHE_BYTES (16)
#define MAX_ANIMATED_BLOCKS (6)

#define SPECTRANET_CACHE_PAGE1 0xC2
#define SPECTRANET_CACHE_PAGE2 0xC3
#define SPECTRANET_CACHE_PAGE3 0xC4
#define SPECTRANET_CACHE_PAGE4 0xC5
#define SPECTRANET_MAP_PAGE   0xC6
#define SPECTRANET_QUERY_PAGE 0xC7
#define SPECTRANET_QUERY2_PAGE 0xC8
#define SPECTRANET_OBJECTS_PAGE 0xC9
#define SPECTRANET_TILES_PAGE 0xCA
#define SPECTRANET_SPRITES_PAGE 0xCB
#define SPECTRANET_MODULES_NAMESPACE0 0xCC
#define SPECTRANET_MODULES_NAMESPACE1_MUSIC0 0xCD
#define SPECTRANET_MODULES_NAMESPACE1_MUSIC1 0xCE
#define SPECTRANET_DATA_PAGES 0xD0

#define NAMESPACE_CODE 0
#define NAMESPACE_MUSIC0 1
#define NAMESPACE_MUSIC1 2

// first page: 0xC0, last: 0xDF

/*
 * Every even byte is tile character
 * Ever odd byte is a sprite character (or 0xFF)
 * These two are the same array mapped onto different pages
 */
extern uint8_t screen_characters_a[1536];
extern uint8_t screen_characters_b[1536];

enum effect_motion_t
{
    EFFECT_MOTION_NONE = 0,
    EFFECT_MOTION_LEFT,
    EFFECT_MOTION_RIGHT,
    EFFECT_MOTION_UP,
    EFFECT_MOTION_DOWN,
    EFFECT_MOTION_RIGHT_3X,
    EFFECT_MOTION_LEFT_3X,
};

struct effect_t
{
    uint8_t rendered;
    uint8_t hidden;
    uint16_t rendered_data_offset;
    uint16_t rendered_xy;
    uint16_t rendered_xy_offset;
    uint8_t dirty;
    uint8_t frame;
    uint8_t frame_tick;
    union {
        struct {
            uint8_t x;
            uint8_t y;
        };
        uint16_t xy;
    };
    int8_t offset_x;
    int8_t offset_y;

    // do not move
    uint8_t data_id;
    uint8_t frames;
    uint8_t rate;
    enum effect_motion_t motion;
};

struct animated_block_t {
    uint8_t xy;
    uint8_t frame;
};

struct cached_chunk_t
{
    uint8_t chunk_id;
    struct animated_block_t animated_blocks[MAX_ANIMATED_BLOCKS];
};

struct client_map_t
{
    struct map_t map;
    uint16_t cached_chunks[MAX_CHUNKS_TO_CACHE];
    struct cached_chunk_t screen_cached_chunks[12];
    uint8_t cached_chunks_dirty[MAX_CHUNKS_TO_CACHE_BYTES];
    uint8_t any_cached_chunks_dirty;
    uint8_t screen_dirty;
    struct chat_message_t chat_messages[MAX_MESSAGES];
};

extern uint8_t animation;
extern uint8_t animation_move;
extern uint8_t animation_pick;
extern uint8_t animation_tick;

struct client_map_effects_t
{
    struct effect_t effect_a;
    struct effect_t effect_b;
    struct effect_t effect_c;
    struct effect_t effect_d;
};

extern void client_map_get_b();

extern uint8_t find_cached_chunk(uint16_t xy) __z88dk_fastcall;
extern void allocate_cached_chunk(uint8_t cached_id, uint16_t xy);
extern uint16_t* memory_switch_cached_chunk_a(uint8_t cached_chunk_id) __z88dk_fastcall;
extern void free_cached_chunk(uint8_t cached_id) __z88dk_fastcall;
extern void client_map_init();
extern void client_map_update_last_known();
extern void client_map_update();
extern void client_map_hide_objects();
extern void client_map_show_objects();
extern void client_map_redraw_objects();
extern void client_map_render();

extern void effects_disable();
extern void effects_enable();

extern void my_player_dirty();
extern void set_cached_chunk_dirty(uint8_t cached_chink_id) __z88dk_fastcall;
extern void clear_cached_chunk_dirty(uint8_t cached_chink_id) __z88dk_fastcall;
extern uint8_t is_cached_chunk_dirty(uint8_t cached_chink_id) __z88dk_fastcall;

extern void show_effect(uint16_t x, uint16_t y, uint8_t* e);

extern void client_map_object_hide(struct map_client_object_t* oo) __z88dk_fastcall;
extern void client_map_object_show(struct map_client_object_t* oo) __z88dk_fastcall;

extern void client_map_get_block_reset_cache();
extern block_t client_map_get_block(uint16_t x, uint16_t y) __z88dk_callee;

#endif
