#ifndef __SERVER_BULLETS_H
#define __SERVER_BULLETS_H

#include "server.h"
#include <ut/uthash.h>

#define BULLETS_GRAVITY (2)
#define BULLETS_SPEED (6)
#define BULLETS_PRECISION 2
#define BULLETS_TTL (1000)

struct server_bullet_synced_t
{
    int client_id;
    UT_hash_handle hh;
};

struct server_bullet_t
{
    uint32_t x;
    uint32_t y;
    uint16_t team_id;
    uint32_t tick;
    uint32_t ttl;
    uint16_t damage;
    uint8_t sound;
    uint8_t contacted;
    struct server_data_entry_t* effect;

    int16_t dx;
    int16_t dy;

    struct server_bullet_synced_t* synced_to;

    struct server_bullet_t* prev;
    struct server_bullet_t* next;
};

extern uint16_t server_bullet_get_x(struct server_bullet_t* bullet);
extern uint16_t server_bullet_get_y(struct server_bullet_t* bullet);
extern void server_bullets_add(struct server_state_t* server_state, uint16_t x, uint16_t y,
    int team_id, uint16_t damage, uint16_t degrees, uint8_t sound, const char* effect);
extern void server_bullets_remove(struct server_state_t* server_state, struct server_bullet_t* bullet);
extern void server_bullets_update(struct server_state_t* server_state);

#endif
