#include "client.h"
#include "client_map.h"
#include <proto.h>
#include <proto_req.h>
#include "object.h"
#include "state.h"
#include <spectranet.h>
#include <spectrum.h>
#include <intrinsic.h>
#include "scenes.h"
#include "zxgui.h"
#include "messages.h"
#include "notifications.h"
#include "soundfx.h"
#include "client_data.h"
#include "client_graphics.h"
#include <sys/socket.h>
#include "printf.h"
#include "modules.h"
#include "vt_sound.h"
#include "hud.h"
#include "particles.h"

static uint16_t x_;
static uint16_t y_;
static uint8_t id_;
static uint16_t code_;
static uint16_t screen_xy_;

static union {
    uint16_t xy;
    struct {
        uint8_t x;
        uint8_t y;
    };
} location;

typedef void (*server_message_cb)(ProtoObject* object) __z88dk_fastcall;

static uint8_t diff_v(uint16_t a, uint16_t b)
{
    a ^= b;
    a &= 0xFFF0;
    return a ? 1 : 0;
}

static void client_set_my_control_object(struct map_client_object_t* oo)
{
    if (my_player_object == oo)
        return;

    if (my_player_object)
    {
        set_object_dirty((&my_player_object->object));
    }

    my_player_object = oo;

    if (my_player_object)
    {
        set_object_dirty((&my_player_object->object));
        my_team_id = oo->object.team_id;
    }
    else
    {
        my_team_id = 0;
    }

    /*
    camera_x = OBJECT_PHY_TO_LOGICAL_CHUNK(oo->object.location.x) - 2;
    camera_y = OBJECT_PHY_TO_LOGICAL_CHUNK(oo->object.location.y) - 1;
    update_camera_bounds();
    client_map_b.screen_dirty = 1;
     */
}

static void client_message_move_obj(ProtoObject* object) __z88dk_fastcall
{
    ProtoObjectPropertyPtr* prop = object->properties;

    while (*prop)
    {
        static ProtoObjectProperty* p;
        p = *prop;

        if (p->key == '_')
        {
            if (p->value_size < sizeof(struct MSG_MOVE_OBJ_t))
            {
                prop++;
                continue;
            }

            struct MSG_MOVE_OBJ_t* move = (struct MSG_MOVE_OBJ_t*)p->value;
            uint8_t object_slot = move->slot;

            if (object_slot < MAX_CLIENT_CACHED_OBJECTS)
            {
                get_objects_a();

                struct map_client_object_t* oo = &map_objects[object_slot];

                memcpy(oo->predictions, move->predictions, sizeof(oo->predictions));

                oo->prediction_ready = 1;
                oo->prediction_offset = 0;

            }
        }

        prop++;
    }
}

static void client_set_object_state(ProtoObject* object) __z88dk_fastcall
{
    struct MSG_OBJ_STATE_t* msg = get_property_ptr(object, '_');

    uint8_t object_slot = msg->object_slot;
    if (object_slot >= MAX_CLIENT_CACHED_OBJECTS)
        return;

    get_objects_a();
    struct map_client_object_t* oo = &map_objects[object_slot];
    struct map_object_t* o = &oo->object;

    o->state = msg->state;
    o->state_flags = msg->state_flags;

    set_object_dirty(o);

    if (oo == my_player_object)
    {
        update_target_marker();
    }
}

static void client_stats(ProtoObject* object) __z88dk_fastcall
{
    my_health = get_uint8_property(object, 'h', 0);
    my_temperature = get_uint8_property(object, 't', 0);
    my_hit_auto = get_uint8_property(object, 'a', 0);
    my_hit_delay = get_uint8_property(object, 'd', 0);
    my_power = get_uint8_property(object, 'p', 0);
    my_credits = get_uint16_property(object, 'c', 0);
    get_str_property(object, '1', my_default_state, sizeof(my_default_state));
    get_str_property(object, '2', my_building_state, sizeof(my_building_state));
    my_stats_dirty = 1;
}

static void client_effect(ProtoObject* object) __z88dk_fastcall
{
    struct MSG_EFFECT_t* msg = get_property_ptr(object, '_');

    if (msg->sound != 0xFF)
    {
        soundfx(msg->sound);
    }

    show_effect(msg->x, msg->y, msg->data);
}

static void client_memory_push(ProtoObject* object) __z88dk_fastcall
{
    uint16_t address = get_uint16_property(object, 'p', 0x4000);
    ProtoObjectProperty* prop = find_property(object, 's');
    memcpy((void*)address, prop->value, prop->value_size);
}

static void client_memory_push_diff(ProtoObject* object) __z88dk_fastcall
{
    static uint8_t* ptr;
    static uint8_t* dst;
    static uint16_t sz;
    ProtoObjectProperty* data = find_property(object, 'd');
    ptr = (uint8_t*)data->value;
    dst = ptr + data->value_size;

    while (ptr < dst)
    {
        uint8_t len = *ptr++;
        uint16_t target = *(uint16_t*)ptr;
        ptr += 2;

        if (len & 0x80)
        {
            // just memory at location
            len &= 0x7F;
            memcpy((void*)target, ptr, len);
            ptr += len;
        }
        else
        {
            // 8 rows of pixels exactly one row apart
            for (uint8_t x = 0 ; x < 8; x++)
            {
                memcpy((void*)target, ptr, len);
                target += 256;
                ptr += len;
            }
        }
    }
}

static void client_module(ProtoObject* object) __z88dk_fastcall
{
    // ignore for now
    uint8_t namespace = get_uint8_property(object, 'n', 0);
    if (namespace >= MODULE_NAMESPACE_COUNT)
        return;

    uint16_t offset = get_uint16_property(object, 'o', 0);
    ProtoObjectProperty* payload = find_property(object, 'p');
    if (payload == NULL)
        return;

    uint8_t* ptr = (uint8_t*)0x1000;
    ptr += offset;

    intrinsic_di();
    setpagea(SPECTRANET_MODULES_NAMESPACE0 + namespace);
    memcpy(ptr, payload->value, payload->value_size);
    get_objects_a();
    module_loaded[namespace] = 1;
    intrinsic_ei();
}


static void client_message_sync(ProtoObject* object) __z88dk_fastcall
{
    struct MSG_SYNC_t* msg = get_property_ptr(object, '_');
    id_ = msg->next_id;
    location.xy = msg->xy;

    allocate_cached_chunk(id_, location.xy);

    uint16_t* target_data = memory_switch_cached_chunk_a(id_);
    memcpy(target_data, msg->chunk_data, MAP_CHUNK_SIZE_DATA_SQ);

    if (location.x >= camera_x && location.y >= camera_y && location.x < camera_x_end && location.y < camera_y_end)
    {
        location.x -= camera_x;
        location.y -= camera_y;

        client_map_b.screen_cached_chunks[location.x + location.y * 4].chunk_id = id_;
    }

    set_cached_chunk_dirty(id_);
}

static void client_message_block_off_screen(ProtoObject* object) __z88dk_fastcall
{
    struct MSG_BLOCK_OFF_SCREEN_t* msg = get_property_ptr(object, '_');
    id_ = msg->id;
    x_ = msg->x;
    y_ = msg->y;

    y_ *= MAP_CHUNK_SIZE;
    y_ += x_;

    static uint16_t* target_data;
    target_data = memory_switch_cached_chunk_a(id_);
    target_data += y_;

    *target_data = msg->code;
}

static void client_message_block_on_screen(ProtoObject* object) __z88dk_fastcall
{
    struct MSG_BLOCK_ON_SCREEN_t* msg = get_property_ptr(object, '_');
    id_ = msg->id;
    x_ = msg->x;
    y_ = msg->y;
    code_ = msg->code;
    screen_xy_ = msg->screen_xy;

    y_ *= MAP_CHUNK_SIZE;
    y_ += x_;

    static union
    {
        struct {
            uint8_t new;
            uint8_t old;
        };
        uint16_t old_new;
    } data;

    static union {
        struct {
            uint8_t code;
            uint8_t flags;
        };
        uint16_t block;
    } *target_data;

    target_data = (void*)memory_switch_cached_chunk_a(id_);
    target_data += y_;

    if ((target_data->flags & 0x7F) || (code_ & 0x7F00))
    {
        // flags are (were) involved – re-cache
        target_data->block = code_;
        set_cached_chunk_dirty(id_);
    }
    else
    {
        my_player_dirty();

        data.old = target_data->code;
        target_data->block = code_;
        data.new = target_data->code;

        if (rendering_blocked)
        {
            return;
        }

        switch_tile_data_a();
        redraw_tile(data.old_new, screen_xy_);
    }
}

static void client_message_unsync(ProtoObject* object) __z88dk_fastcall
{
    id_ = get_uint8_property(object, 'i', 0);
    free_cached_chunk(id_);
}

static void client_message_set_object_client_id(ProtoObject* object) __z88dk_fastcall
{
    uint8_t object_slot = get_uint8_property(object, 's', 0);

    if (object_slot >= MAX_CLIENT_CACHED_OBJECTS)
        return;

    uint16_t client_id = get_uint16_property(object, 'i', 0);

    get_objects_a();

    struct map_client_object_t* oo = &map_objects[object_slot];
    oo->object.client_id = client_id;

    if (oo == my_player_object && client_id != my_client_id)
    {
        client_set_my_control_object(NULL);
        update_target_marker();
        return;
    }

    if ((oo->object.f0 & OBJECT_F0_USED) == 0)
    {
        return;
    }

    if (client_id == my_client_id)
    {
        client_set_my_control_object(oo);
    }
}

static void client_message_sync_obj(ProtoObject* object) __z88dk_fastcall
{
    uint8_t object_slot = get_uint8_property(object, 's', 0);
    if (object_slot >= MAX_CLIENT_CACHED_OBJECTS)
        return;

    get_objects_a();
    struct map_client_object_t* oo = &map_objects[object_slot];

    struct map_object_t* o = &oo->object;

    if (o->f0)
        return;

    oo->rendered = 0;
    oo->prediction_ready = 0;
    oo->prediction_offset = 0xFF;
    ProtoObjectProperty* p = find_property(object, '_');

    if (p == NULL || p->value_size < MAP_OBJECT_SYNC_CRITICAL_SIZE)
    {
        return;
    }

    memcpy(&o->object_id, p->value, p->value_size);

    o->f0 = OBJECT_F0_DIRTY | OBJECT_F0_USED | OBJECT_F0_LOOKING_LEFT;
    o->f1 = 0;

    init_object(o);
    update_object_sprite(o, 0, 0, 0);

    if (o->client_id == my_client_id)
    {
        client_set_my_control_object(oo);
    }

    client_map_update_last_known();
}


static void client_message_unsync_obj(ProtoObject* object) __z88dk_fastcall
{
    struct MSG_UNSYNC_OBJ_t* msg = get_property_ptr(object, '_');

    uint8_t object_slot = msg->slot;
    if (object_slot >= MAX_CLIENT_CACHED_OBJECTS)
        return;

    get_objects_a();
    struct map_client_object_t* oo = &map_objects[object_slot];
    struct map_object_t* o = &map_objects[object_slot].object;

    if (o->object_id != msg->object_id)
        return;

    if (my_player_object == oo)
    {
        my_player_object = NULL;
    }

    if (rendering_blocked == 0)
    {
        client_map_object_hide(oo);
    }

    set_cached_chunk_dirty(msg->sync_chunk_id);

    memset(oo, 0, sizeof(struct map_client_object_t));

    client_map_update_last_known();
}

static void client_message_chat(ProtoObject* object) __z88dk_fastcall
{
    ProtoObjectProperty* m = find_property(object, 'm');
    if (m == NULL)
        return;

    char_msg(m->value, m->value_size);
}

static void client_message_notification(ProtoObject* object) __z88dk_fastcall
{
    ProtoObjectProperty* m = find_property(object, 'm');
    if (m == NULL)
        return;

    show_notification(m->value, m->value_size, get_uint8_property(object, 'c', INK_WHITE | PAPER_BLACK));
}

static void client_message_progress(ProtoObject* object) __z88dk_fastcall
{
    uint8_t progress = get_uint8_property(object, 'p', 0);

    if (progress >= 12)
    {
        clear_scheduled_touch();
        clear_notification_progress();
    }
    else
    {
        show_notification_progress(progress);
    }
}

static void client_message_force_query_result(ProtoObject* object) __z88dk_fastcall
{
    (void)object;
}

static void client_watch(ProtoObject* object) __z88dk_fastcall
{
    struct MSG_WATCH_t* msg = get_property_ptr(object, '_');

    isr_render_enabled = 0;

    skip_rendering_while_dirty = 1;

    camera_x = msg->x;
    camera_y = msg->y;
    update_camera_bounds();

    uint8_t* ptr = msg->chunks;

    for (uint8_t i = 0; i < 12; i++)
    {
        client_map_b.screen_cached_chunks[i].chunk_id = *ptr;
        ptr++;
    }

    memset(client_map_b.cached_chunks_dirty, 0xFF, MAX_CHUNKS_TO_CACHE_BYTES);
    client_map_b.screen_dirty = 1;

    isr_render_enabled = 1;
}


static void client_ula_write(ProtoObject* object) __z88dk_fastcall
{
    screen_border = get_uint8_property(object, 'u', 0);
#ifndef __CLION_IDE__
    #asm
    ld a, (_screen_border)
    out (254),a
#endasm
#endif
}

static void music_init() __naked
{
#ifndef __CLION_IDE__
    #asm
    extern PUSHPAGEA
    extern PUSHPAGEB
    extern POPPAGEA
    extern POPPAGEB
    extern _vt_init

    ld a, SPECTRANET_MODULES_NAMESPACE1_MUSIC0
    call PUSHPAGEA
    ld a, SPECTRANET_MODULES_NAMESPACE1_MUSIC1
    call PUSHPAGEB

    ld hl, 0x1000
    call _vt_init

    call POPPAGEB
    call POPPAGEA
    ret
#endasm
#endif
}

static void client_module_action(ProtoObject* object) __z88dk_fastcall
{
    static uint8_t namespace;
    namespace = get_uint8_property(object, 'n', 0);

    switch(namespace)
    {
        case NAMESPACE_MUSIC0:
        {
            static uint8_t active;
            active = get_uint8_property(object, 'e', 0);
            if (active)
            {
                vt_mute();
                music_init();

                // reset the complete trigger
                vt_setup_byte &= ~(0x80);
            }
            else
            {
                vt_mute();
            }

            module_music_active = active;
            break;
        }
        case NAMESPACE_CODE:
        default:
        {
            if (namespace >= MODULE_NAMESPACE_COUNT || module_loaded[namespace] == 0)
                break;

            module_call_namespace = namespace;
            setpagea(SPECTRANET_MODULES_NAMESPACE0 + namespace);
            module_action(object);
            get_objects_a();

            break;
        }
    }
}

static void client_bullet(ProtoObject* object) __z88dk_fastcall
{
    struct MSG_BULLET_t *msg = get_property_ptr(object, '_');

    if (my_player_object == NULL)
        return;

    uint16_t xx = OBJECT_PHY_TO_LOGICAL(msg->x);
    uint16_t yy = OBJECT_PHY_TO_LOGICAL(msg->y);

    if (xx < (camera_base_x) || yy < camera_base_y)
        return;

    xx -= camera_base_x;
    yy -= camera_base_y;

    if (xx >= 32 || yy >= 24)
        return;

    xx *= 8;
    yy *= 8;

    xx += msg->x % 8;
    yy += msg->y % 8;

    add_particle(xx, yy, msg->dx, msg->dy, msg->ttl, msg->sound);

    if (msg->effect)
    {
        show_effect(msg->x - 8, msg->y, msg->effect_data);
    }
}

static server_message_cb server_message_callbacks[] = {
        // MSG_MOVE_OBJ,
        client_message_move_obj,
        // MSG_SYNC,
        client_message_sync,
        // MSG_BLOCK_OFF_SCREEN,
        client_message_block_off_screen,
        // MSG_BLOCK_ON_SCREEN,
        client_message_block_on_screen,
        // MSG_UNSYNC,
        client_message_unsync,
        // MSG_SYNC_OBJ,
        client_message_sync_obj,
        // MSG_UNSYNC_OBJ,
        client_message_unsync_obj,
        // MSG_CHAT,
        client_message_chat,
        // MSG_NOTIFY,
        client_message_notification,
        // MSG_PROGRESS,
        client_message_progress,
        // MSG_OBJ_STATE,
        client_set_object_state,
        // MSG_YOUR_STATS,
        client_stats,
        // MSG_MEMORY_PUSH,
        client_memory_push,
        // MSG_MEMORY_PUSH_DIFF,
        client_memory_push_diff,
        // MSG_EFFECT,
        client_effect,
        // MSG_FORCE_QUERY_RESULT,
        client_message_force_query_result,
        // MSG_WATCH
        client_watch,
        // MSG_MODULE
        client_module,
        // MSG_MODULE_ACTION
        client_module_action,
        // MSG_ULA_WRITE
        client_ula_write,
        // MSG_OBJ_SET_CLIENT_ID
        client_message_set_object_client_id,
        // MSG_BULLET
        client_bullet
};

void client_message_object(ProtoObject* object, void* user)
{
    static server_message_cb cb;
    if (process_proto.recv_objects_num == 0)
    {
        static uint8_t id;
        id = get_uint8_property(object, OBJ_PROPERTY_ID, 0);
        cb = server_message_callbacks[id];
        cb(object);
    }
    else
    {
        cb(object);
    }
}

const char* client_message_complete(void* user)
{
    return NULL;
}

void client_new_message(void* user)
{
}
