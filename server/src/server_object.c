#include "server_object.h"
#include "server_map.h"
#include "server.h"
#include "messages.h"
#include "object.h"
#include "utils.h"

static uint8_t diff_v(int32_t a, int32_t b, uint32_t range)
{
    return abs(a - b) <= range;
}

static uint16_t object_motion_axis_distance(uint16_t a, uint16_t b)
{
    return (uint16_t)abs((int32_t)a - (int32_t)b);
}

static uint8_t object_motion_profile_step(uint8_t profile, uint8_t phase, uint16_t remaining, uint16_t start_distance)
{
    if (profile == SERVER_OBJECT_MOTION_PROFILE_ACCELERATING)
    {
        uint16_t distance = 0;
        if (start_distance > remaining)
        {
            distance = start_distance - remaining;
        }

        if (remaining == 0)
        {
            distance = 0;
        }

        if (distance >= 64)
        {
            return 2;
        }
        if (distance >= 32)
        {
            return 1;
        }
        if (distance >= 16)
        {
            return (phase % 2) == 0 ? 1 : 0;
        }
        if (remaining > 0)
        {
            return (phase % 4) == 0 ? 1 : 0;
        }
        return 0;
    }

    if (remaining > 64)
    {
        return 2;
    }
    if (remaining > 32)
    {
        return 1;
    }
    if (remaining > 16)
    {
        return (phase % 2) == 0 ? 1 : 0;
    }
    if (remaining > 0)
    {
        return (phase % 4) == 0 ? 1 : 0;
    }
    return 0;
}

static uint8_t generate_object_predictions_step(struct server_object_reference_t* ref)
{
    struct map_object_t* o = &ref->object;

    if (o->type & MAP_OBJECT_SLOW)
    {
        if (ref->slow)
        {
            ref->slow = 0;
            return 0;
        }

        ref->slow = 1;
    }

    if (ref->move_to_target)
    {
        server_object_move_to_target(ref);

        if ((o->speed.x == 0) && (o->speed.y == 0) &&
            (o->location.x == o->target.x) && (o->location.y == o->target.y))
        {
            ref->move_to_target = 0;
        }
    }
    else if (ref->adjust_target)
    {
        server_object_adjust_target(o);

        if ((o->adjustment_speed.x == 0) && (o->adjustment_speed.y == 0))
        {
            ref->adjust_target = 0;
        }
    }

    update_object(o);
    return 1;
}

static uint8_t object_prediction_diverged(
    const struct object_prediction_t* a,
    const struct object_prediction_t* b)
{
    return abs((int32_t)a->x - (int32_t)b->x) > SERVER_OBJECT_PREDICTION_DIVERGENCE ||
        abs((int32_t)a->y - (int32_t)b->y) > SERVER_OBJECT_PREDICTION_DIVERGENCE;
}

static uint8_t object_predictions_diverged(struct server_object_reference_t* ref)
{
    if (!ref->previous_predictions_valid)
    {
        return 1;
    }

    static const uint8_t check_frames[] = { 4, 12, 23 };

    for (uint8_t i = 0; i < sizeof(check_frames); i++)
    {
        const uint8_t idx = check_frames[i];

        if (object_prediction_diverged(&ref->previous_predictions[idx], &ref->predictions[idx]))
        {
            return 1;
        }
    }

    return 0;
}

void server_object_clear_overlaps(struct server_object_reference_t* ref)
{
    struct server_object_overlap_t* overlap = NULL;
    struct server_object_overlap_t* tmp = NULL;

    HASH_ITER(hh, ref->overlapped_blocks, overlap, tmp)
    {
        HASH_DELETE(hh, ref->overlapped_blocks, overlap);
        free(overlap);
    }
}

static void server_object_add_current_overlap(
    struct server_object_overlap_t** current,
    uint16_t x,
    uint16_t y)
{
    struct server_object_overlap_t* exists = NULL;
    const uint16_t xy = map_chunk_xy(x, y);

    HASH_FIND(hh, *current, &xy, sizeof(uint16_t), exists);
    if (exists)
    {
        return;
    }

    exists = calloc(1, sizeof(struct server_object_overlap_t));
    exists->xy = xy;
    HASH_ADD(hh, *current, xy, sizeof(uint16_t), exists);
}

static void server_object_update_block_overlaps(
    struct server_state_t* server_state,
    struct server_object_reference_t* ref)
{
    struct map_t* map = &server_state->map.map;
    struct map_object_t* o = &ref->object;
    struct server_object_overlap_t* current = NULL;
    struct server_object_overlap_t* overlap = NULL;
    struct server_object_overlap_t* tmp = NULL;

    const uint16_t left = OBJECT_PHY_TO_LOGICAL(o->location.x);
    const uint16_t right = OBJECT_PHY_TO_LOGICAL(o->location.x + 15);
    const uint16_t top = OBJECT_PHY_TO_LOGICAL(o->location.y > 15 ? o->location.y - 15 : 0);
    const uint16_t bottom = OBJECT_PHY_TO_LOGICAL(o->location.y);

    for (uint16_t y = top; y <= bottom; y++)
    {
        if (y >= map->height * MAP_CHUNK_SIZE)
        {
            continue;
        }

        for (uint16_t x = left; x <= right; x++)
        {
            if (x >= map->width * MAP_CHUNK_SIZE)
            {
                continue;
            }

            server_object_add_current_overlap(&current, x, y);

            const uint16_t xy = map_chunk_xy(x, y);
            struct server_object_overlap_t* previous = NULL;
            HASH_FIND(hh, ref->overlapped_blocks, &xy, sizeof(uint16_t), previous);
            if (previous == NULL)
            {
                server_python_map_call_block_overlap(map, x, y, ref);
            }
        }
    }

    HASH_ITER(hh, ref->overlapped_blocks, overlap, tmp)
    {
        struct server_object_overlap_t* still_overlapping = NULL;
        HASH_FIND(hh, current, &overlap->xy, sizeof(uint16_t), still_overlapping);
        if (still_overlapping == NULL)
        {
            HASH_DELETE(hh, ref->overlapped_blocks, overlap);
            free(overlap);
        }
    }

    HASH_ITER(hh, current, overlap, tmp)
    {
        struct server_object_overlap_t* previous = NULL;
        HASH_FIND(hh, ref->overlapped_blocks, &overlap->xy, sizeof(uint16_t), previous);
        if (previous == NULL)
        {
            HASH_DELETE(hh, current, overlap);
            HASH_ADD(hh, ref->overlapped_blocks, xy, sizeof(uint16_t), overlap);
        }
    }

    HASH_ITER(hh, current, overlap, tmp)
    {
        HASH_DELETE(hh, current, overlap);
        free(overlap);
    }
}

void generate_object_predictions(struct server_object_reference_t* ref)
{
    struct server_object_reference_t simulation = *ref;
    const uint16_t animation = ref->prediction_animation;
    uint8_t stationary = 1;
    uint16_t prev_x = ref->object.location.x;
    uint16_t prev_y = ref->object.location.y;

    for (uint8_t i = 0; i < OBJECT_PREDICTION_FRAMES; i++)
    {
        // see animation_tick on client
        const uint16_t frame = animation + (i / 2);

        generate_object_predictions_step(&simulation);
        update_object_sprite(
            &simulation.object,
            simulation.object.speed.x + simulation.object.adjustment_speed.x,
            24 * (frame % 4),
            32 * ((frame >> 1) % 4));

        ref->predictions[i].x = simulation.object.location.x;
        ref->predictions[i].y = simulation.object.location.y;
        ref->predictions[i].sprite_data_id = simulation.object.sprite_data_id;
        ref->predictions[i].sprite_offset = simulation.object.sprite_offset;

        if (simulation.object.location.x != prev_x || simulation.object.location.y != prev_y)
        {
            stationary = 0;
        }
        else
        {
            if (i != 0)
            {
                /* Stationary playback still animates (e.g. picking); keep sprite ids for sync. */
                if (simulation.object.state != OBJECT_STATE_PICKING)
                {
                    /* "no movement" — omit redundant sprite in predictions */
                    ref->predictions[i].sprite_data_id = 0xFF;
                }
                else
                {
                    stationary = 0;
                }
            }
        }

        prev_x = simulation.object.location.x;
        prev_y = simulation.object.location.y;
    }

    ref->predictions_stationary = stationary;
    ref->predictions_diverged = object_predictions_diverged(ref);
    memcpy(ref->previous_predictions, ref->predictions, sizeof(ref->previous_predictions));
    ref->previous_predictions_valid = 1;

    const uint8_t moving = !stationary;
    if (moving != ref->predictions_were_moving)
    {
        ref->force_sync = 1;
    }

    ref->predictions_were_moving = moving;

    long now = server_time();
    if (now > ref->prediction_animation_time_ms)
    {
        // update according to animation_tick on client
        ref->prediction_animation_time_ms = now + SERVER_OBJECT_SPRITE_ANIMATION_FRAME_MS;
        ref->prediction_animation++;
    }
}

void update_server_object(struct server_state_t* server_state, struct server_object_reference_t* ref)
{
    struct map_object_t* o = &ref->object;

    if (ref->destroyed)
        return;

    if (!generate_object_predictions_step(ref))
    {
        server_object_update_block_overlaps(server_state, ref);
        generate_object_predictions(ref);
        return;
    }

    server_object_update_block_overlaps(server_state, ref);

    if (IS_F0_SET(o, OBJECT_F0_COLLIDED))
    {
        RESET_F0(o, OBJECT_F0_COLLIDED);

        server_python_on_block_collide_object_py(&server_state->server_python, ref);

        if (ref->destroyed)
            return;
    }

    if (o->landed_after_fall)
    {
        const int8_t fall_speed = o->fall_speed;
        o->landed_after_fall = 0;
        o->fall_speed = 0;

        server_python_on_fall_object_py(&server_state->server_python, ref, fall_speed);

        if (ref->destroyed)
            return;
    }

    {
        struct server_object_reference_t *check_ref, *tmp;
        HASH_ITER(hh, server_state->map.objects, check_ref, tmp)
        {
            if (ref == check_ref)
                continue;
            if (check_ref->destroyed)
                continue;

            if (diff_v(ref->object.location.x, check_ref->object.location.x, 16) &&
                diff_v(ref->object.location.y, check_ref->object.location.y, 16))
            {
                server_python_object_contact_py(&server_state->server_python, ref, check_ref);
                server_python_object_contact_py(&server_state->server_python, check_ref, ref);
            }
        }
    }

    generate_object_predictions(ref);
}

void sync_server_object(struct server_state_t* server_state, struct server_object_reference_t* ref)
{
    struct map_object_t* o = &ref->object;

    long now = server_time();
    uint8_t should_sync = ref->force_sync;

    if (!should_sync)
    {
        if (now <= ref->sync_cooldown)
        {
            return;
        }

        ref->sync_cooldown = now + SERVER_OBJECT_PREDICTION_SYNC_RATE;

        if (ref->predictions_stationary)
        {
            return;
        }

        should_sync = ref->predictions_diverged || (now >= ref->next_prediction_sync_ms);
        if (!should_sync)
        {
            return;
        }
    }
    else
    {
        ref->sync_cooldown = now + SERVER_OBJECT_PREDICTION_SYNC_RATE;
    }

    server_python_object_on_move(&server_state->server_python, ref);

    struct client_state_t* send_to;
    LL_FOREACH(get_server_state()->client_states, send_to)
    {
        if ((ref->force_sync == 0) && (send_to->client_id == o->client_id))
            continue;

        uint8_t slot = server_state_client_find_synced_object(send_to, get_server_state(), o);
        if (slot == 0xFF)
        {
            continue;
        }

        int object_id = ref->object.object_id;
        struct client_object_sync_queue_t* exists = NULL;
        HASH_FIND_INT(send_to->object_sync_queue, &object_id, exists);

        if (exists == NULL)
        {
            struct client_object_sync_queue_t* sync = calloc(1, sizeof(struct client_object_sync_queue_t));
            sync->object_id = object_id;
            sync->ref = ref;
            sync->slot = slot;
            memcpy(sync->predictions, ref->predictions, sizeof(sync->predictions));

            HASH_ADD_INT(send_to->object_sync_queue, object_id, sync);

            if (send_to->object_sync_time == 0)
            {
                send_to->object_sync_time = server_time() + CLIENT_OBJECT_SYNC_BATCHING;
            }
        }
        else
        {
            memcpy(exists->predictions, ref->predictions, sizeof(exists->predictions));
        }
    }

    ref->next_prediction_sync_ms = now + SERVER_OBJECT_PREDICTION_PERIODIC_SYNC_RATE;
    ref->predictions_diverged = 0;
    ref->force_sync = 0;
    server_python_sync_object_py(&server_state->server_python, ref);
}

void set_object_dirty(struct map_object_t* o)
{
    SET_F0(o, OBJECT_F0_DIRTY);
}

uint8_t server_object_serialize(struct server_state_t* server_state, struct server_object_reference_t* ref,
    ProtoObject** serialize_to)
{
    PyObject* dict = server_python_object_serialize(&server_state->server_python, ref);
    if (dict == NULL)
    {
        return 1;
    }

    declare_arg_property_on_stack(client_id, 'c', ref->client_id, NULL);
    declare_arg_property_on_stack(id, 'i', ref->object.object_id, &client_id);
    // physical
    declare_arg_property_on_stack(x, 'x', ref->object.location.x, &id);
    declare_arg_property_on_stack(y, 'y', ref->object.location.y, &x);
    *serialize_to = py_dict_to_proto_object(dict, &y);

    Py_DecRef(dict);
    return 0;
}

uint8_t server_object_deserialize(struct server_state_t* server_state, ProtoObject* proto_object,
    struct server_object_reference_t* ref)
{
    PyObject* dict = proto_object_to_py_dict(proto_object);

    ref->client_id = get_uint16_property(proto_object, 'c', 0);
    //ref->object.object_id = get_uint16_property(proto_object, 'i', 0);
    // physical
    // ref->object.location.x = get_uint16_property(proto_object, 'x', 0);
    // ref->object.location.y = get_uint16_property(proto_object, 'y', 0);
    ref->object.target.x = ref->object.location.x;
    ref->object.target.y = ref->object.location.y;
    ref->object.speed.x = 0;
    ref->object.speed.y = 0;

    if (server_python_object_deserialize(&server_state->server_python, ref, dict))
    {
        return 0;
    }

    Py_DecRef(dict);
    return 0;
}


void server_object_adjust_target(struct map_object_t* o)
{
    o->target.x += o->speed.x;
    o->target.y += o->speed.y;

    if (o->target.x > o->location.x + OBJECT_TARGET_ADJUST)
    {
        o->adjustment_speed.x = 1;
    }
    else if (o->target.x < o->location.x - OBJECT_TARGET_ADJUST)
    {
        o->adjustment_speed.x = -1;
    }
    else
    {
        o->adjustment_speed.x = 0;
    }

    if (o->target.y > o->location.y + OBJECT_TARGET_ADJUST)
    {
        o->adjustment_speed.y = 1;
    }
    else if (o->target.y < o->location.y - OBJECT_TARGET_ADJUST)
    {
        o->adjustment_speed.y = -1;
    }
    else
    {
        o->adjustment_speed.y = 0;
    }
}

void server_object_move_to_target(struct server_object_reference_t* ref)
{
    struct map_object_t* o = &ref->object;

    if (o->target.x > o->location.x)
    {
        o->speed.x = 1;
    }
    else if (o->target.x < o->location.x)
    {
        o->speed.x = -1;
    }
    else
    {
        o->speed.x = 0;
    }

    if (IS_F0_SET(o, OBJECT_F0_FIXING) || (o->type & MAP_OBJECT_STATIC))
    {
        const uint16_t remaining_y = object_motion_axis_distance(o->target.y, o->location.y);

        if (o->target.y > o->location.y)
        {
            if (ref->motion_profile == SERVER_OBJECT_MOTION_PROFILE_NONE)
            {
                o->speed.y = 1;
            }
            else
            {
                const uint8_t step = object_motion_profile_step(
                    ref->motion_profile,
                    ref->motion_profile_phase,
                    remaining_y,
                    ref->motion_profile_start_distance);
                o->speed.y = (int8_t)step;
            }
        }
        else if (o->target.y < o->location.y)
        {
            if (ref->motion_profile == SERVER_OBJECT_MOTION_PROFILE_NONE)
            {
                o->speed.y = -1;
            }
            else
            {
                const uint8_t step = object_motion_profile_step(
                    ref->motion_profile,
                    ref->motion_profile_phase,
                    remaining_y,
                    ref->motion_profile_start_distance);
                o->speed.y = (int8_t)-step;
            }
        }
        else
        {
            o->speed.y = 0;
        }
    }

    if (ref->motion_profile != SERVER_OBJECT_MOTION_PROFILE_NONE)
    {
        ref->motion_profile_phase++;
    }
}
