#ifndef __CLIENT_OBJECT_H
#define __CLIENT_OBJECT_H

#include "env.h"
#include "object.h"

struct map_client_object_t
{
    struct map_object_t object;
    struct object_prediction_t predictions[OBJECT_PREDICTION_FRAMES];

    uint8_t rendered;
    uint8_t jumping;
    uint8_t rendered_sprite_data_id;
    uint16_t rendered_sprite_offset;
    uint8_t prediction_ready;
    uint8_t prediction_offset;

    union {
        struct {
            uint8_t x;
            uint8_t y;
        };
        uint16_t xy;
    } last_local_coords, last_local_offset;
};

// NOTE: if you update this, take care to update client_map_redraw_objects

#endif
