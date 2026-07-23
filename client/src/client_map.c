#include "client_map.h"
#include "client.h"
#include "state.h"
#include "client_object.h"
#include "spectranet.h"
#include <spectrum.h>
#include "client_graphics.h"
#include "client_data.h"
#include "frame.h"
#include "messages.h"
#include "modules.h"
#include "vt_sound.h"
#include "particles.h"
#include "hud.h"

void client_map_init()
{
    switch_sprite_data_b();
    memset(screen_characters_b, 0, sizeof(screen_characters_b));

    for (uint16_t i = 1; i < 1536; i += 2)
    {
        screen_characters_b[i] = 0x7F;
    }

    client_map_get_b();
    client_map_b.any_cached_chunks_dirty = 0;
    client_map_b.screen_dirty = 0;
    animation = 0;

    memset(client_map_b.cached_chunks, 0xFF, sizeof(uint16_t) * MAX_CHUNKS_TO_CACHE);
    memset(client_map_b.screen_cached_chunks, 0xFF, sizeof(client_map_b.screen_cached_chunks));
    memset(client_map_b.cached_chunks_dirty, 0, MAX_CHUNKS_TO_CACHE_BYTES);
    memset(client_map_b.chat_messages, 0, sizeof(client_map_b.chat_messages));

    get_objects_a();
    memset(map_objects, 0, sizeof(struct map_client_object_t) * MAX_CLIENT_CACHED_OBJECTS);
    map_objects_last_known_size = 0;
}

void client_map_get_b() __naked
{
#ifndef __CLION_IDE__
#asm
    extern SETPAGEB
    ld a, SPECTRANET_MAP_PAGE
    jp SETPAGEB
#endasm
#endif
}

static uint16_t i_;
static uint16_t x_;
static uint16_t y_;
static uint16_t last_search = 0xFFFF;
static uint8_t last_result;

uint8_t find_cached_chunk(uint16_t xy) __z88dk_fastcall
{
    if (last_search == xy)
    {
        return last_result;
    }

    uint16_t* ch = client_map_b.cached_chunks;

    for (i_ = 0; i_ < MAX_CHUNKS_TO_CACHE; i_++, ch++)
    {
        if (*ch == xy)
        {
            last_search = xy;
            last_result = i_;
            return i_;
        }
    }

    return 0xFF;
}

void allocate_cached_chunk(uint8_t cached_id, uint16_t xy)
{
    client_map_b.cached_chunks[cached_id] = xy;
}

void free_cached_chunk(uint8_t existing) __z88dk_fastcall
{
    client_map_b.cached_chunks[existing] = 0xFFFF;
    set_cached_chunk_dirty(existing);
}

uint16_t* memory_switch_cached_chunk_a(uint8_t cached_chunk_id) __z88dk_fastcall
{
    setpagea(SPECTRANET_CACHE_PAGE1 + (cached_chunk_id / 32));
    return (uint16_t*)((uint8_t*)0x1000 + (cached_chunk_id & 0x1F) * MAP_CHUNK_SIZE_DATA_SQ);
}

static void client_map_calculate_chunk(struct cached_chunk_t* cached_chunk, uint8_t ox, uint8_t oy) __z88dk_callee
{
    uint8_t chunk_id = cached_chunk->chunk_id;

    if (chunk_id == 0xFF)
        return;

    uint8_t* data_at = (uint8_t*) memory_switch_cached_chunk_a(chunk_id);

    uint8_t yy = oy == 16 ? (MAP_CHUNK_SIZE - 1) : MAP_CHUNK_SIZE;
    uint8_t yyy = oy == 0 ? 1 : 0;

    if (oy == 0)
    {
        // skip first row of source
        data_at += MAP_CHUNK_SIZE * 2;
    }

    struct animated_block_t* ab = cached_chunk->animated_blocks;
    struct animated_block_t* ab_end = ab + MAX_ANIMATED_BLOCKS;

    memset(ab, 0xFF, sizeof(cached_chunk->animated_blocks));
    switch_sprite_data_b();

    for (y_ = yyy; y_ < yy; y_++)
    {

        uint8_t* p = &screen_characters_b[(y_ + oy) * 64 + ox * 2];

        for (x_ = 0; x_ < MAP_CHUNK_SIZE; x_++)
        {
            *p = *data_at++;
            p += 2;

            uint8_t flags = *data_at++;
            if ((flags & BLOCK_FLAG_ANIMATED) && (ab < ab_end))
            {
                client_map_get_b();
                ab->xy = x_ | (y_ << 4);
                ab->frame = (flags & 0x0F) << 4;
                ab++;
                switch_sprite_data_b();
            }
        }
    }

    client_map_get_b();
}

struct precompiled_offset_t {
    uint8_t cx;
    uint8_t cy;
    uint8_t scx;
    uint8_t scy;
};

void my_player_dirty()
{
    if (my_player_object)
    {
        my_player_object->object.f0 |= OBJECT_F0_DIRTY;
        my_player_object->object.flags_at.x = 0;
        my_player_object->object.flags_at.y = 0;
    }
}

void set_cached_chunk_dirty(uint8_t cached_chink_id) __z88dk_fastcall
{
    my_player_dirty();

    if (cached_chink_id == 0xFF)
        return;

    client_map_b.any_cached_chunks_dirty = 1;
    client_map_b.cached_chunks_dirty[cached_chink_id / 8] |= (1 << (cached_chink_id % 8));
}

void clear_cached_chunk_dirty(uint8_t cached_chink_id) __z88dk_fastcall
{
    if (cached_chink_id == 0xFF)
        return;

    client_map_b.any_cached_chunks_dirty = 1;
    client_map_b.cached_chunks_dirty[cached_chink_id / 8] &= ~(1 << (cached_chink_id % 8));
}

uint8_t is_cached_chunk_dirty(uint8_t cached_chink_id) __z88dk_fastcall
{
    if (cached_chink_id == 0xFF)
        return 0;

    return client_map_b.cached_chunks_dirty[cached_chink_id / 8] & (1 << (cached_chink_id % 8));
}

static void client_map_render_chunk(struct cached_chunk_t* cached_chunk, struct precompiled_offset_t* off) __z88dk_callee
{
    uint8_t chunk_id = cached_chunk->chunk_id;

    if (chunk_id == 0xFF)
        return;

    if (!is_cached_chunk_dirty(chunk_id))
        return;

    clear_cached_chunk_dirty(chunk_id);

    effects_disable();

    static uint16_t ix1;
    static uint16_t iy1;
    static uint16_t ix2;
    static uint16_t iy2;

    ix1 = off->scx + camera_x;
    iy1 = off->scy + camera_y;

    ix2 = ix1 + MAP_CHUNK_SIZE;
    iy2 = iy1 + MAP_CHUNK_SIZE;

    get_objects_a();
    struct map_client_object_t* oo = map_objects;

    for (uint8_t i = 0; i < map_objects_last_known_size; i++, oo++)
    {
        static struct map_object_t *o;
        o = &oo->object;

        if (o->f0 == 0)
            continue;

        static uint16_t xx;
        static uint16_t yy;

        xx = OBJECT_PHY_TO_LOGICAL(o->location.x);
        yy = OBJECT_PHY_TO_LOGICAL(o->location.y);

        if (xx >= ix1 && xx < ix2 && yy >= iy1 && yy < iy2)
        {
            // mark every object within that chunk as dirty
            SET_F0(o, OBJECT_F0_DIRTY);

            // drop flags_at
            o->flags_at.x = 0;
            o->flags_at.y = 0;
        }
    }

    client_map_calculate_chunk(cached_chunk, off->scx, off->scy);

    switch_tile_data_a();
    switch_sprite_data_b();
    render_chunk(off->scx | (off->scy << 8));
    get_objects_a();
    client_map_get_b();

    effects_enable();
}

static uint8_t i;

void client_map_update_last_known()
{
    // update the size so there wouldn't be wasteful iteration
    map_objects_last_known_size = 0;

    struct map_client_object_t* oo = map_objects;
    for (i = 0; i < MAX_CLIENT_CACHED_OBJECTS; i++, oo++)
    {
        struct map_object_t *o = &oo->object;
        if (o->f0 == 0)
            continue;

        map_objects_last_known_size = i + 1;
    }
}

static struct precompiled_offset_t precompiled_offsets[12] = {
        {0, 0, 0,                   0},
        {1, 0, MAP_CHUNK_SIZE,      0},
        {2, 0, 2 * MAP_CHUNK_SIZE,  0},
        {3, 0, 3 * MAP_CHUNK_SIZE,  0},
        {0, 1, 0,                   MAP_CHUNK_SIZE},
        {1, 1, MAP_CHUNK_SIZE,      MAP_CHUNK_SIZE},
        {2, 1, 2 * MAP_CHUNK_SIZE,  MAP_CHUNK_SIZE},
        {3, 1, 3 * MAP_CHUNK_SIZE,  MAP_CHUNK_SIZE},
        {0, 2, 0,                   MAP_CHUNK_SIZE * 2},
        {1, 2, MAP_CHUNK_SIZE,      MAP_CHUNK_SIZE * 2},
        {2, 2, 2 * MAP_CHUNK_SIZE,  MAP_CHUNK_SIZE * 2},
        {3, 2, 3 * MAP_CHUNK_SIZE,  MAP_CHUNK_SIZE * 2},
};

static void update_chunk_animated_blocks(struct cached_chunk_t* cc, struct precompiled_offset_t* offset)
{
    struct animated_block_t* ab = cc->animated_blocks;

    for (i = 0; i < MAX_ANIMATED_BLOCKS; i++, ab++)
    {
        if (ab->xy == 0xFF)
        {
            break;
        }

        static union
        {
            struct
            {
                uint8_t x;
                uint8_t y;
            };
            uint16_t xy;
        } coords;

        coords.x = offset->scx + (ab->xy & 0x0F);
        coords.y = offset->scy + ((ab->xy & 0xF0) >> 4);
        uint8_t* p = screen_characters_a;
        p += (uint16_t)coords.x * 2;
        p += (uint16_t)coords.y * 64;
        ab->frame++;

        static union
        {
            struct {
                uint8_t new;
                uint8_t old;
            };
            uint16_t old_new;
        } data;

        data.old = *p;

        (*p)++;

        uint8_t max_frames = (ab->frame & 0xF0) >> 4;

        if ((ab->frame & 0x0F) >= max_frames)
        {
            *p -= max_frames;
            ab->frame = ab->frame & 0xF0;
        }

        data.new = *p;

        switch_tile_data_a();
        redraw_tile(data.old_new, coords.xy);
        switch_sprite_data_a();
    }
}

static uint8_t diff_v(uint16_t a, uint16_t b)
{
    a ^= b;
    a &= 0xFFF0;
    return a ? 1 : 0;
}

static uint8_t diff_b(int16_t a, int16_t b)
{
    uint8_t a_ = a > 0 ? 1 : (a < 0 ? 2 : 0);
    uint8_t b_ = b > 0 ? 1 : (b < 0 ? 2 : 0);
    return a_ != b_;
}

void client_map_hide_objects()
{
    get_objects_a();

    struct map_client_object_t* oo = map_objects;
    for (i = 0; i < map_objects_last_known_size; i++, oo++ )
    {
        struct map_object_t *o = &oo->object;

        if (o->f0 == 0)
            continue;

        if (IS_F0_SET(o, OBJECT_F0_HIDDEN))
            continue;

        SET_F0(o, OBJECT_F0_HIDDEN);
        client_map_object_hide(oo);
    }
}

void client_map_show_objects()
{
    get_objects_a();

    struct map_client_object_t* oo = map_objects;
    for (i = 0; i < map_objects_last_known_size; i++, oo++ )
    {
        struct map_object_t *o = &oo->object;

        if (IS_F0_SET(o, OBJECT_F0_HIDDEN) == 0)
            continue;

        RESET_F0(o, OBJECT_F0_HIDDEN);
        client_map_object_show(oo);
    }
}

void client_map_update()
{
    if (rendering_blocked == 0 && ++animation_tick >= 2)
    {
        animation_tick = 0;
        animation++;

        if (animation % 4 == 0)
        {
            struct cached_chunk_t* cc = client_map_b.screen_cached_chunks;
            struct precompiled_offset_t* oo = precompiled_offsets;
            switch_sprite_data_a();
            for (uint8_t i = 0; i < 12; i++)
            {
                update_chunk_animated_blocks(cc++, oo++);
            }
            get_objects_a();
        }
    }

    animation_move = 24 * (animation % 4);
    animation_pick = 32 * ((animation >> 1) % 4);

    get_objects_a();

    for (i = 0; i < map_objects_last_known_size; i++)
    {
        struct map_client_object_t* oo = &map_objects[i];
        struct map_object_t* o = &oo->object;

        if (o->f0 == 0 || oo == my_player_object || oo->prediction_ready == 0)
            continue;

        const struct object_prediction_t* prediction = &oo->predictions[oo->prediction_offset];

        // 0xFF means no movement
        if (prediction->sprite_data_id != 0xFF)
        {
            o->sprite_data_id = prediction->sprite_data_id;
            o->location.x = prediction->x;
            o->location.y = prediction->y;
            o->sprite_offset = prediction->sprite_offset;

            set_object_dirty(o);
        }


        oo->prediction_offset++;

        if (oo->prediction_offset >= OBJECT_PREDICTION_FRAMES)
        {
            oo->prediction_ready = 0;
        }
    }

    if (my_player_object)
    {
        update_object(&my_player_object->object);

        static struct {
            uint16_t x;
            uint16_t y;
        } last_sync_location = {};

        static struct {
            int8_t x;
            int8_t y;
        } last_sync_speed = {};

        if (diff_b(last_sync_speed.x, my_player_object->object.speed.x) ||
            diff_b(last_sync_speed.y, my_player_object->object.speed.y) ||
            diff_v(last_sync_location.x, my_player_object->object.location.x) ||
            diff_v(last_sync_location.y, my_player_object->object.location.y))
        {
            last_sync_speed.x = my_player_object->object.speed.x;
            last_sync_speed.y = my_player_object->object.speed.y;
            last_sync_location.x = my_player_object->object.location.x;
            last_sync_location.y = my_player_object->object.location.y;

            declare_str_property_on_stack(req_id, OBJ_PROPERTY_ID, MSG_MOVE, NULL);
            declare_arg_property_on_stack(_x, 'x', my_player_object->object.location.x, &req_id);
            declare_arg_property_on_stack(_y, 'y', my_player_object->object.location.y, &_x);
            declare_arg_property_on_stack(_speed_x, 'X', my_player_object->object.speed.x, &_y);
            declare_arg_property_on_stack(_speed_y, 'Y', my_player_object->object.speed.y, &_speed_x);
            declare_object_on_stack(request, 32, &_speed_y);

            proto_send_one_nf(request);
        }
    }
}

void do_render_screen()
{
    render_screen();
}

static void client_map_redraw_screen()
{
    rendering_blocked = 1;
    my_stats_dirty = 1;
    client_map_b.screen_dirty = 0;
    memset(client_map_b.cached_chunks_dirty, 0, MAX_CHUNKS_TO_CACHE_BYTES);

    clear_particles();
    clear_hud();
    get_objects_a();

    for (i = 0; i < map_objects_last_known_size; i++)
    {
        struct map_client_object_t *oo = &map_objects[i];
        struct map_object_t *o = &oo->object;

        if (o->f0 == 0)
            continue;

        // mark every object as dirty
        SET_F0(o, OBJECT_F0_DIRTY);

        // drop rendered flag so it is not hidden afterward
        oo->rendered = 0;

        // drop flags_at
        o->flags_at.x = 0;
        o->flags_at.y = 0;
    }

    struct precompiled_offset_t* off = precompiled_offsets;
    struct cached_chunk_t* chu = client_map_b.screen_cached_chunks;

    for (uint8_t ccc = 0; ccc < 12; ccc++, off++, chu++)
    {
        client_map_calculate_chunk(chu, off->scx, off->scy);
    }

    // redraw everything
    switch_tile_data_a();
    switch_sprite_data_b();
    do_render_screen();
    get_objects_a();
    client_map_get_b();

    render_particles();
    render_hud();

    skip_rendering_while_dirty = 0;
    rendering_blocked = 0;
}

static void client_map_redraw_chunks()
{
    client_map_render_chunk(&client_map_b.screen_cached_chunks[0], &precompiled_offsets[0]);
    client_map_render_chunk(&client_map_b.screen_cached_chunks[1], &precompiled_offsets[1]);
    client_map_render_chunk(&client_map_b.screen_cached_chunks[4], &precompiled_offsets[4]);
    client_map_render_chunk(&client_map_b.screen_cached_chunks[5], &precompiled_offsets[5]);
    client_map_render_chunk(&client_map_b.screen_cached_chunks[8], &precompiled_offsets[8]);
    client_map_render_chunk(&client_map_b.screen_cached_chunks[9], &precompiled_offsets[9]);
    client_map_render_chunk(&client_map_b.screen_cached_chunks[2], &precompiled_offsets[2]);
    client_map_render_chunk(&client_map_b.screen_cached_chunks[3], &precompiled_offsets[3]);
    client_map_render_chunk(&client_map_b.screen_cached_chunks[6], &precompiled_offsets[6]);
    client_map_render_chunk(&client_map_b.screen_cached_chunks[7], &precompiled_offsets[7]);
    client_map_render_chunk(&client_map_b.screen_cached_chunks[10], &precompiled_offsets[10]);
    client_map_render_chunk(&client_map_b.screen_cached_chunks[11], &precompiled_offsets[11]);
}

void client_map_object_hide(struct map_client_object_t* oo) __z88dk_fastcall
{
    if (oo->rendered == 0)
        return;

    static uint8_t* sprite_data;
    sprite_data = switch_data_entry_a(oo->rendered_sprite_data_id);
    sprite_data += oo->rendered_sprite_offset;

    if (oo->rendered_sprite_data_id & 0x80)
    {
        render_sprite_pre_shifted(sprite_data, oo->last_local_offset.xy, oo->last_local_coords.xy);
    }
    else
    {
        render_sprite(sprite_data, oo->last_local_offset.xy, oo->last_local_coords.xy);
    }

    oo->rendered = 0;
}

void client_map_object_show(struct map_client_object_t* oo) __z88dk_fastcall
{
    if (oo->rendered)
        return;

    static uint8_t* sprite_data;
    sprite_data = switch_data_entry_a(oo->object.sprite_data_id);
    sprite_data += oo->object.sprite_offset;

    if (oo->object.sprite_data_id & 0x80)
    {
        render_sprite_pre_shifted(sprite_data, oo->last_local_offset.xy, oo->last_local_coords.xy);
    }
    else
    {
        render_sprite(sprite_data, oo->last_local_offset.xy, oo->last_local_coords.xy);
    }

    oo->rendered_sprite_data_id = oo->object.sprite_data_id;
    oo->rendered_sprite_offset = oo->object.sprite_offset;
    oo->rendered = 1;
}

static void client_map_redraw_object(struct map_client_object_t* oo) __z88dk_fastcall
{
    struct map_object_t* o = &oo->object;

    if (!IS_F0_SET(o, OBJECT_F0_DIRTY))
        return;

    RESET_F0(o, OBJECT_F0_DIRTY);

    static uint16_t phy_x;
    static uint16_t phy_y;

    phy_x = o->location.x;
    phy_y = o->location.y;

    if (phy_x < camera_low_phy_x || phy_y < camera_low_phy_y ||
        phy_x >= camera_high_phy_x || phy_y >= camera_high_phy_y)
    {
        client_map_object_hide(oo);
        return;
    }

    if (o->state_flags & STATE_FLAG_FLASHING)
    {
        // renew dirty
        set_object_dirty(o);

        // hide ourselves every other frame
        if (animation & 2)
        {
            client_map_object_hide(oo);
            return;
        }
    }


    static union {
        struct {
            uint8_t x;
            uint8_t y;
        };
        uint16_t xy;
    } offsets, coords;

    offsets.x = phy_x & 7;
    offsets.y = phy_y & 7;

    coords.x = (phy_x - camera_low_phy_x) >> 3;
    coords.y = (phy_y - camera_low_phy_y) >> 3;

    if (oo->rendered)
    {
        client_map_object_hide(oo);
    }

    oo->last_local_offset.xy = offsets.xy;
    oo->last_local_coords.xy = coords.xy;

    if (oo == my_player_object)
    {
        hud_dirty = 1;
    }

    client_map_object_show(oo);
}


void client_map_redraw_objects() __naked
{
#ifndef __CLION_IDE__
#asm
    ld a, (_rendering_blocked)
    or a
    ret nz

    ld a, (_skip_rendering_while_dirty)
    or a
    ret nz

    ld a, (_map_objects_last_known_size)
    or a
    ret z

    ld hl, _map_objects

loop_redraw_objects:
    push af
    push hl
    call _client_map_redraw_object
    pop hl
    pop af
    dec a
    ret z

    ld bc, 207
    add hl, bc
    jr loop_redraw_objects
#endasm
#else
    // NOTE: THIS CODE DOES NOT COMPILE, THIS IS OPTIMIZED OUT
    // SEE ABOVE

    if (rendering_blocked)
        return;

    if (skip_rendering_while_dirty)
        return;

    struct map_client_object_t* oo = map_objects;
    for (uint8_t i = 0; i < map_objects_last_known_size; i++, oo++)
    {
        client_map_redraw_object(oo);
    }
#endif
}

static void effect_hide(struct effect_t* effect) __z88dk_fastcall
{
    if (effect->rendered == 0)
        return;

#ifndef __JETBRAINS_IDE__
#asm
    di
#endasm
#endif

    uint8_t* sprite_data = switch_data_entry_a(effect->data_id);

    render_sprite(sprite_data + effect->rendered_data_offset,
                  effect->rendered_xy_offset,
                  effect->rendered_xy);

#ifndef __JETBRAINS_IDE__
#asm
    ei
#endasm
#endif

    effect->rendered = 0;
}

static void effect_show(struct effect_t* effect) __z88dk_fastcall
{
    if (effect->rendered)
        return;

#ifndef __JETBRAINS_IDE__
#asm
    di
#endasm
#endif

    uint8_t* sprite_data = switch_data_entry_a(effect->data_id);
    effect->rendered_data_offset = 32 * effect->frame;
    effect->rendered_xy = (effect->y << 8) | effect->x;
    effect->rendered_xy_offset = *(uint16_t *) &effect->offset_x;

    render_sprite(sprite_data + effect->rendered_data_offset,
                  effect->rendered_xy_offset,
                  effect->rendered_xy);

#ifndef __JETBRAINS_IDE__
#asm
    ei
#endasm
#endif

    effect->rendered = 1;
}

static void effect_disable(struct effect_t* effect) __z88dk_fastcall
{
    if (!effect->rendered)
        return;

    effect->hidden = 1;
    effect_hide(effect);
}

static void effect_enable(struct effect_t* effect) __z88dk_fastcall
{
    if (!effect->hidden)
        return;

    effect->hidden = 0;
    effect_show(effect);
}

void effects_disable()
{
    effect_disable(&map_effects.effect_a);
    effect_disable(&map_effects.effect_b);
    effect_disable(&map_effects.effect_c);
    effect_disable(&map_effects.effect_d);
}

void effects_enable()
{
    effect_enable(&map_effects.effect_a);
    effect_enable(&map_effects.effect_b);
    effect_enable(&map_effects.effect_c);
    effect_enable(&map_effects.effect_d);
}

static void render_effect_sprite(struct effect_t* effect) __z88dk_fastcall
{
    if (effect->data_id == 0)
        return;

    if (animation_tick)
        return;

    effect->frame_tick++;

    if (effect->frame_tick >= effect->rate)
    {
        effect->dirty = 1;
        effect->frame_tick = 0;
        effect->frame++;
    }

    if (effect->frame >= effect->frames)
    {
        effect_hide(effect);
        effect->data_id = 0;
        return;
    }

    switch (effect->motion) {
        case EFFECT_MOTION_RIGHT_3X:
        {
            effect->offset_x++;
            effect->offset_x++;
        } // fallthrough
        case EFFECT_MOTION_RIGHT:
        {
            effect->offset_x++;
            if (effect->offset_x >= 8)
            {
                effect->offset_x = 0;
                effect_hide(effect);
                effect->x++;
            }
            break;
        }
        case EFFECT_MOTION_LEFT_3X:
        {
            effect->offset_x--;
            effect->offset_x--;
        } // fallthrough
        case EFFECT_MOTION_LEFT:
        {
            effect->offset_x--;
            if (effect->offset_x <= 0)
            {
                effect->offset_x = 7;
                effect_hide(effect);
                effect->x--;
            }
            break;
        }
        case EFFECT_MOTION_UP:
        {
            effect->offset_y--;
            if (effect->offset_y <= 0)
            {
                effect->offset_y = 7;
                effect_hide(effect);
                effect->y--;
            }
            break;
        }
        case EFFECT_MOTION_DOWN:
        {
            effect->offset_y++;
            if (effect->offset_y >= 8)
            {
                effect->offset_y = 0;
                effect_hide(effect);
                effect->y++;
            }
            break;
        }
        default:
        {
            break;
        }
    }

    if (effect->rendered)
    {
        if (effect->dirty == 0)
            return;

        effect_hide(effect);
    }

    effect_show(effect);
}

void show_effect(uint16_t x, uint16_t y, uint8_t* e)
{
    struct effect_t* effect;
    if (map_effects.effect_a.data_id == 0)
    {
        effect = &map_effects.effect_a;
    }
    else if (map_effects.effect_b.data_id == 0)
    {
        effect = &map_effects.effect_b;
    }
    else if (map_effects.effect_c.data_id == 0)
    {
        effect = &map_effects.effect_c;
    }
    else if (map_effects.effect_d.data_id == 0)
    {
        effect = &map_effects.effect_d;
    }
    else
    {
        return;
    }

    static uint16_t xx;
    static uint16_t yy;

    xx = OBJECT_PHY_TO_LOGICAL(x);
    yy = OBJECT_PHY_TO_LOGICAL(y);

    if (xx < camera_base_x || yy < camera_base_y)
    {
        return;
    }

    static uint8_t local_x;
    static uint8_t local_y;

    local_x = xx - camera_base_x;
    local_y = yy - camera_base_y - 1;

    if (local_x >= 30 || local_y >= 22)
    {
        return;
    }

    effect->frame = 0;

    // copy data_id, frames, rate, motion
    memcpy(&effect->data_id, e, 4);

    effect->frame_tick = 0;
    effect->x = local_x;
    effect->y = local_y;

    effect->offset_x = x & 7;
    effect->offset_y = y & 7;

    effect->rendered = 0;
    effect->dirty = 1;
}

void client_map_render()
{
    if (rendering_blocked || panel)
    {
        return;
    }

    if (my_stats_dirty)
    {
        render_my_stats();
    }

    if (client_map_b.screen_dirty)
    {
        client_map_b.any_cached_chunks_dirty = 0;

        isr_render_enabled = 0;
        client_map_redraw_screen();
        render_my_stats();
        isr_render_enabled = 1;
    }
    else
    {
        if (client_map_b.any_cached_chunks_dirty)
        {
            if (rendering_blocked)
            {
                return;
            }

            isr_render_enabled = 0;
            client_map_b.any_cached_chunks_dirty = 0;
            client_map_hide_objects();
            hide_particles();
            hide_hud();
            client_map_redraw_chunks();
            client_map_show_objects();
            render_hud();
            show_particles();
            render_my_stats();
            isr_render_enabled = 1;
        }
        else
        {
            // client_map_redraw_objects is called from ISR
        }
    }

    if (unique_frame)
    {
        render_effect_sprite(&map_effects.effect_a);
        render_effect_sprite(&map_effects.effect_b);
        render_effect_sprite(&map_effects.effect_c);
        render_effect_sprite(&map_effects.effect_d);
    }
}

static uint8_t last_x = 0xFF;
static uint8_t last_y = 0xFF;
static uint16_t* last_chunk_data = NULL;

void client_map_get_block_reset_cache()
{
    last_x = 0xFF;
    last_y = 0xFF;
}

block_t client_map_get_block(uint16_t x, uint16_t y) __z88dk_callee
{
    static uint8_t _x;
    static uint8_t _y;

    _x = x >> 3;
    _y = y >> 3;

    if (last_x != _x || last_y != _y)
    {
        last_x = _x;
        last_y = _y;

        if (_x >= camera_x && _x < camera_x_end && _y >= camera_y && _y < camera_y_end)
        {
            _x -= camera_x;
            _x += (_y - camera_y) * 4;
            _y = client_map_b.screen_cached_chunks[_x].chunk_id;
        }
        else
        {
            _y = find_cached_chunk(map_chunk_xy(_x, _y));
        }

        if (_y == 0xFF)
            return 0xFF;

        last_chunk_data = memory_switch_cached_chunk_a(_y);
    }

    _x = x & 0x7;
    _y = y & 0x7;

    return last_chunk_data[_x + _y * MAP_CHUNK_SIZE];
}
