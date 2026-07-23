#ifndef __SERVER_OBJECT_H
#define __SERVER_OBJECT_H

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <proto_objects.h>

#include "object.h"
#include "uthash.h"

struct server_state_t;

#define SERVER_OBJECT_PREDICTION_SYNC_RATE 20
#define SERVER_OBJECT_PREDICTION_PERIODIC_SYNC_RATE 200
// pixels
#define SERVER_OBJECT_PREDICTION_DIVERGENCE 16

/* Client sprite animation advances every two 50Hz movement frames. */
#define SERVER_OBJECT_SPRITE_ANIMATION_FRAME_MS 40

#define SERVER_OBJECT_MOTION_PROFILE_NONE 0
#define SERVER_OBJECT_MOTION_PROFILE_SLOWING_DOWN 1
#define SERVER_OBJECT_MOTION_PROFILE_ACCELERATING 2

struct server_object_overlap_t
{
    uint16_t xy;
    UT_hash_handle hh;
};

struct server_object_reference_t
{
    struct map_object_t object;
    PyObject* py_object;
    uint16_t client_id;
    char name[9];

    long sync_cooldown;
    uint8_t force_sync;
    uint8_t adjust_target;
    uint8_t move_to_target;
    uint8_t motion_profile;
    uint8_t motion_profile_phase;
    uint16_t motion_profile_start_distance;
    uint16_t motion_profile_target_x;
    uint16_t motion_profile_target_y;
    struct object_prediction_t predictions[OBJECT_PREDICTION_FRAMES];
    struct object_prediction_t previous_predictions[OBJECT_PREDICTION_FRAMES];
    long next_prediction_sync_ms;
    uint8_t prediction_animation;
    /** Last server_time()(ms) boundary used for sprite animation prediction steps. */
    long prediction_animation_time_ms;
    uint8_t predictions_stationary;
    uint8_t previous_predictions_valid;
    uint8_t predictions_diverged;
    uint8_t predictions_were_moving;

    struct server_object_overlap_t* overlapped_blocks;

    uint8_t slow;
    uint8_t destroyed;
    UT_hash_handle hh;
};

extern uint8_t server_object_serialize(struct server_state_t* server_state, struct server_object_reference_t* ref,
    ProtoObject** serialize_to);

extern uint8_t server_object_deserialize(struct server_state_t* server_state, ProtoObject* proto_object,
    struct server_object_reference_t* ref);

extern void update_server_object(struct server_state_t* server_state, struct server_object_reference_t* ref);
extern void sync_server_object(struct server_state_t* server_state, struct server_object_reference_t* ref);
extern void server_object_move_to_target(struct server_object_reference_t* ref);
extern void server_object_adjust_target(struct map_object_t* o);
extern void generate_object_predictions(struct server_object_reference_t* ref);
extern void server_object_clear_overlaps(struct server_object_reference_t* ref);

#endif
