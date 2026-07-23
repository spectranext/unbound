#ifndef __MESSAGES_H
#define __MESSAGES_H

#include "object.h"

enum server_to_client
{
    MSG_MOVE_OBJ = 0,
    MSG_SYNC,
    MSG_BLOCK_OFF_SCREEN,
    MSG_BLOCK_ON_SCREEN,
    MSG_UNSYNC,
    MSG_SYNC_OBJ,
    MSG_UNSYNC_OBJ,
    MSG_CHAT,
    MSG_NOTIFY,
    MSG_PROGRESS,
    MSG_OBJ_STATE,
    MSG_YOUR_STATS,
    MSG_MEMORY_PUSH,
    MSG_MEMORY_PUSH_DIFF,
    MSG_EFFECT,
    MSG_FORCE_QUERY_RESULT,
    MSG_WATCH,
    MSG_MODULE,
    MSG_MODULE_ACTION,
    MSG_ULA_WRITE,
    MSG_OBJ_SET_CLIENT_ID,
    MSG_BULLET
};

#ifdef SPECTRUM
#define PACKED__
#else
#define PACKED__ __attribute__((__packed__))
#endif

struct PACKED__ MSG_SYNC_t
{
    uint8_t next_id;
    union {
        struct {
            uint8_t x;
            uint8_t y;
        };
        uint16_t xy;
    };
    block_t chunk_data[MAP_CHUNK_SIZE_SQ];
};

struct PACKED__ MSG_BLOCK_OFF_SCREEN_t
{
    uint8_t id;
    uint8_t x;
    uint8_t y;
    block_t code;
};

struct PACKED__ MSG_BLOCK_ON_SCREEN_t
{
    uint8_t id;
    uint8_t x;
    uint8_t y;
    block_t code;

    union
    {
        struct {
            uint8_t screen_x;
            uint8_t screen_y;
        };
        uint16_t screen_xy;
    };
};

struct PACKED__ MSG_EFFECT_t
{
    uint16_t x;
    uint16_t y;
    uint8_t sound;
    uint8_t data[4];
};

struct PACKED__ MSG_WATCH_t
{
    uint8_t x;
    uint8_t y;
    uint8_t chunks[12];
};

struct PACKED__ MSG_UNSYNC_OBJ_t
{
    uint8_t sync_chunk_id;
    uint16_t object_id;
    uint8_t slot;
};

struct PACKED__ MSG_OBJ_STATE_t
{
    uint8_t object_slot;
    uint8_t state;
    uint8_t state_flags;
};

struct PACKED__ MSG_BULLET_t
{
    uint16_t x;
    uint16_t y;
    int8_t dx;
    int8_t dy;
    uint8_t ttl;
    uint8_t sound;
    uint8_t effect;
    uint8_t effect_data[4];
};

struct PACKED__ MSG_MOVE_OBJ_t
{
    uint8_t slot;
    struct object_prediction_t predictions[OBJECT_PREDICTION_FRAMES];
};

#define MSG_TERMINAL            "tr"
#define MSG_CLIENT_CHAT         "ch"
#define MSG_QUERY_OPTION        "qo"
#define MSG_QUERY               "qu"
#define MSG_AUTH                "au"
#define MSG_MOVE                "mo"
#define MSG_TOUCH               "to"
#define MSG_TOUCH_CANCEL        "tc"
#define MSG_HIT                 "hi"
#define MSG_AIM                 "ai"
#define MSG_ACTION              "ac"
#define MSG_DOWNLOAD            "dw"
#define MSG_CLIENT_BINARY       "bi"

#endif
