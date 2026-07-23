#include "server_bullets.h"
#include "ut/utlist.h"
#include "utils.h"
#include "messages.h"
#include "server_data.h"

uint16_t server_bullet_get_x(struct server_bullet_t* bullet)
{
    return bullet->x >> BULLETS_PRECISION;
}

uint16_t server_bullet_get_y(struct server_bullet_t* bullet)
{
    return bullet->y >> BULLETS_PRECISION;
}

void server_bullets_add(struct server_state_t* server_state,  uint16_t x, uint16_t y,
    int team_id, uint16_t damage, uint16_t degrees, uint8_t sound, const char* effect)
{
    struct server_bullet_t* bullet = calloc(1, sizeof(struct server_bullet_t));

    bullet->x = (uint32_t)(x + 8) << BULLETS_PRECISION;
    bullet->y = (uint32_t)(y) << BULLETS_PRECISION;
    bullet->team_id = team_id;
    bullet->damage = damage;
    bullet->ttl = BULLETS_TTL;
    bullet->sound = sound;

    if (effect)
    {
        bullet->effect = find_data_entry(&server_state->server_data, effect);
    }

    double deg = degrees + (double )((double)(rand() % 400) / 100.f) - 2.f;
    double d = degrees_to_radians(deg);

    bullet->dx = (int16_t)(sin(d) * (BULLETS_SPEED << BULLETS_PRECISION));
    bullet->dy = (int16_t)(cos(d) * (BULLETS_SPEED << BULLETS_PRECISION));

    bullet->x += bullet->dx;
    bullet->y += bullet->dy;

    DL_APPEND(server_state->bullets, bullet);
}

void server_bullets_remove(struct server_state_t* server_state, struct server_bullet_t* bullet)
{
    struct server_bullet_synced_t* sync;
    struct server_bullet_synced_t* tmp;

    HASH_ITER(hh, bullet->synced_to, sync, tmp)
    {
        HASH_DEL(bullet->synced_to, sync);
        free(sync);
    }

    DL_DELETE(server_state->bullets, bullet);
    free(bullet);
}

static uint8_t bullet_point_hits_object(uint16_t x, uint16_t y, uint16_t left, uint16_t top,
    uint16_t right, uint16_t bottom)
{
    return x >= left && x <= right && y >= top && y <= bottom;
}

static int32_t bullet_line_direction(uint16_t ax, uint16_t ay, uint16_t bx, uint16_t by,
    uint16_t cx, uint16_t cy)
{
    return ((int32_t)cx - (int32_t)ax) * ((int32_t)by - (int32_t)ay) -
        ((int32_t)cy - (int32_t)ay) * ((int32_t)bx - (int32_t)ax);
}

static uint8_t bullet_value_between(uint16_t value, uint16_t a, uint16_t b)
{
    if (a > b)
    {
        uint16_t tmp = a;
        a = b;
        b = tmp;
    }

    return value >= a && value <= b;
}

static uint8_t bullet_point_on_segment(uint16_t ax, uint16_t ay, uint16_t bx, uint16_t by,
    uint16_t px, uint16_t py)
{
    return bullet_line_direction(ax, ay, bx, by, px, py) == 0 &&
        bullet_value_between(px, ax, bx) &&
        bullet_value_between(py, ay, by);
}

static uint8_t bullet_segments_intersect(uint16_t ax, uint16_t ay, uint16_t bx, uint16_t by,
    uint16_t cx, uint16_t cy, uint16_t dx, uint16_t dy)
{
    int32_t d1 = bullet_line_direction(cx, cy, dx, dy, ax, ay);
    int32_t d2 = bullet_line_direction(cx, cy, dx, dy, bx, by);
    int32_t d3 = bullet_line_direction(ax, ay, bx, by, cx, cy);
    int32_t d4 = bullet_line_direction(ax, ay, bx, by, dx, dy);

    if (d1 == 0 && bullet_point_on_segment(cx, cy, dx, dy, ax, ay))
        return 1;
    if (d2 == 0 && bullet_point_on_segment(cx, cy, dx, dy, bx, by))
        return 1;
    if (d3 == 0 && bullet_point_on_segment(ax, ay, bx, by, cx, cy))
        return 1;
    if (d4 == 0 && bullet_point_on_segment(ax, ay, bx, by, dx, dy))
        return 1;

    return ((d1 <= 0 && d2 >= 0) || (d1 >= 0 && d2 <= 0)) &&
        ((d3 <= 0 && d4 >= 0) || (d3 >= 0 && d4 <= 0)) &&
        d1 != 0 && d2 != 0 && d3 != 0 && d4 != 0;
}

static uint8_t bullet_path_hits_object(uint16_t from_x, uint16_t from_y, uint16_t to_x, uint16_t to_y,
    struct map_object_t* object)
{
    uint16_t left = object->location.x;
    uint16_t right = left + 15;
    uint16_t bottom = object->location.y;
    uint16_t top = bottom >= 15 ? bottom - 15 : 0;

    if (bullet_point_hits_object(from_x, from_y, left, top, right, bottom) ||
        bullet_point_hits_object(to_x, to_y, left, top, right, bottom))
    {
        return 1;
    }

    return bullet_segments_intersect(from_x, from_y, to_x, to_y, left, top, right, top) ||
        bullet_segments_intersect(from_x, from_y, to_x, to_y, right, top, right, bottom) ||
        bullet_segments_intersect(from_x, from_y, to_x, to_y, right, bottom, left, bottom) ||
        bullet_segments_intersect(from_x, from_y, to_x, to_y, left, bottom, left, top);
}

static uint8_t should_sync_bullet_to_client(struct server_state_t* server_state, struct server_bullet_t* bullet,
    struct client_state_t* client_state)
{
    return 1;
}

static uint8_t predict_bullet_ttl_for_client(struct server_state_t* server_state, struct server_bullet_t* bullet,
    struct client_state_t* client_state)
{
    uint32_t x = bullet->x;
    uint32_t y = bullet->y;
    uint32_t tick = bullet->tick;

    int16_t dx = bullet->dx;
    int16_t dy = bullet->dy;

    uint8_t ticks = 0;

    uint32_t ttl = bullet->ttl;
    if (ttl > 255)
    {
        ttl = 255;
    }

    while (ttl--)
    {
        ticks++;

        // blocking
        if (map_get_block(&get_server_state()->map.map, OBJECT_PHY_TO_LOGICAL((x >> BULLETS_PRECISION)),
            OBJECT_PHY_TO_LOGICAL((y >> BULLETS_PRECISION))) & 0x8000)
        {
            break;
        }

        x += dx;
        y += dy;
        tick++;

        if (tick % BULLETS_GRAVITY == 0)
        {
            dy++;
        }
    }

    return ticks;
}

static void sync_bullet_to_client(struct server_state_t* server_state, struct server_bullet_t* bullet,
    struct client_state_t* client_state)
{
    struct server_bullet_synced_t* sync = calloc(1, sizeof(struct server_bullet_synced_t));
    sync->client_id = client_state->client_id;
    HASH_ADD_INT(bullet->synced_to, client_id, sync);

    struct MSG_BULLET_t msg = {
        .x = server_bullet_get_x(bullet),
        .y = server_bullet_get_y(bullet),
        .dx = (int8_t)bullet->dx,
        .dy = (int8_t)bullet->dy,
        .ttl = predict_bullet_ttl_for_client(server_state, bullet, client_state),
        .sound = bullet->sound
    };

    if (bullet->effect && bullet->tick == 0)
    {
        uint8_t frames = get_data_entry_prop_int(bullet->effect, "FRAMES", 0);
        uint8_t rate = get_data_entry_prop_int(bullet->effect, "RATE", 0);
        uint8_t motion = get_data_entry_prop_int(bullet->effect, "MOTION", 0);

        msg.effect = 1;
        uint8_t d[4] = {bullet->effect->index, frames, rate, motion};
        memcpy(msg.effect_data, d, sizeof(d));
    }

    declare_arg_property_on_stack(_msg, '_', msg, NULL);
    uint8_t command = MSG_BULLET;
    declare_arg_property_on_stack(id, OBJ_PROPERTY_ID, command, &_msg);

    client_state_send_proto_one_object(server_state, client_state, &id);
}

void server_bullets_update(struct server_state_t* server_state)
{
    struct server_bullet_t* bullet;
    struct server_bullet_t* tmp;

    DL_FOREACH_SAFE(server_state->bullets, bullet, tmp)
    {
        if (map_get_block(&get_server_state()->map.map, OBJECT_PHY_TO_LOGICAL(server_bullet_get_x(bullet)),
            OBJECT_PHY_TO_LOGICAL(server_bullet_get_y(bullet))) & 0x8000)
        {
            server_bullets_remove(server_state, bullet);
            continue;
        }

        if (--bullet->ttl == 0)
        {
            server_bullets_remove(server_state, bullet);
            continue;
        }

        struct client_state_t* client_state;
        LL_FOREACH(server_state->client_states, client_state)
        {
            struct server_bullet_synced_t* sync = NULL;
            int client_id = client_state->client_id;
            HASH_FIND_INT(bullet->synced_to, &client_id, sync);

            if (sync != NULL)
                continue;

            if (should_sync_bullet_to_client(server_state, bullet, client_state))
            {
                sync_bullet_to_client(server_state, bullet, client_state);
            }
        }

        uint16_t prev_x = server_bullet_get_x(bullet);
        uint16_t prev_y = server_bullet_get_y(bullet);

        bullet->x += bullet->dx;
        bullet->y += bullet->dy;
        bullet->tick++;

        if (bullet->contacted == 0)
        {
            uint16_t xx = server_bullet_get_x(bullet);
            uint16_t yy = server_bullet_get_y(bullet);

            struct server_object_reference_t* object;
            struct server_object_reference_t* tmp2;
            HASH_ITER(hh, server_state->map.objects, object, tmp2)
            {
                if (object->py_object == NULL)
                    continue;
                if (object->object.team_id == bullet->team_id)
                    continue;
                if (!bullet_path_hits_object(prev_x, prev_y, xx, yy, &object->object))
                    continue;
                server_python_damage_object_py(&server_state->server_python, object, bullet->damage, "bullet");
                bullet->contacted = 1;
            }
        }

        if (bullet->tick % BULLETS_GRAVITY == 0)
        {
            bullet->dy++;
        }
    }
}
