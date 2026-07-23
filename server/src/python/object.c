#include "server_python.h"
#include "server.h"

static uint16_t object_profile_distance_for_target(struct server_object_reference_t* ref, uint16_t x, uint16_t y)
{
    uint16_t dx = (uint16_t)abs((int32_t)x - (int32_t)ref->object.location.x);
    uint16_t dy = (uint16_t)abs((int32_t)y - (int32_t)ref->object.location.y);

    if (dy > dx)
    {
        return dy;
    }
    return dx;
}

PyObject* object_api_set_state_flags_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    object_obj* obj = (object_obj*)callable;

    struct server_object_reference_t* ref = server_map_get_object(
            &obj->server_python->server_state->map, obj->object_id);

    if (ref != NULL)
    {
        uint8_t flags = PyLong_AsLong(PyTuple_GetItem(args, 0));
        map_set_object_state_flags(obj->server_python->server_state,
                                   &obj->server_python->server_state->map.map, ref, flags);
    }

    Py_RETURN_NONE;
}

PyObject* object_api_set_object_state_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    object_obj* obj = (object_obj*)callable;

    struct server_object_reference_t* ref = server_map_get_object(
            &obj->server_python->server_state->map, obj->object_id);

    if (ref != NULL)
    {
        uint8_t state = PyLong_AsLong(PyTuple_GetItem(args, 0));
        map_set_object_state(obj->server_python->server_state, &obj->server_python->server_state->map.map, ref, state);
    }

    Py_RETURN_NONE;
}

PyObject* object_api_reset_object_state_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    object_obj* obj = (object_obj*)callable;

    struct server_object_reference_t* ref = server_map_get_object(
            &obj->server_python->server_state->map, obj->object_id);

    if (ref != NULL)
    {
        server_python_yield_team(obj->server_python, ref);
        server_python_yield_object_state(obj->server_python, ref);
    }

    Py_RETURN_NONE;
}

PyObject* object_api_destroy_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    object_obj* obj = (object_obj*)callable;
    map_mark_object_to_delete(obj->server_python->server_state, &obj->server_python->server_state->map.map, obj->object_id);
    Py_RETURN_NONE;
}

PyObject* object_api_move_to_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    object_obj* obj = (object_obj*)callable;

    PyObject* xx = PyTuple_GetItem(args, 0);
    PyObject* yy = PyTuple_GetItem(args, 1);

    uint16_t x;
    uint16_t y;

    if (PyFloat_Check(xx))
    {
        double xd = PyFloat_AsDouble(xx);

        if (server_python_check_err())
        {
            Py_RETURN_NONE;
        }

        x = (uint16_t)xd;
        x = OBJECT_LOGICAL_TO_PHY(x) + (uint16_t)((double)(xd - x) * 8.0f);
    }
    else
    {
        x = OBJECT_LOGICAL_TO_PHY(PyLong_AsLong(xx));
    }

    if (PyFloat_Check(yy))
    {
        double yd = PyFloat_AsDouble(yy);

        if (server_python_check_err())
        {
            Py_RETURN_NONE;
        }

        y = (uint16_t)yd;
        y = OBJECT_LOGICAL_TO_PHY(y) + (uint16_t)((double)(yd - y) * 8.0f);
    }
    else
    {
        y = OBJECT_LOGICAL_TO_PHY(PyLong_AsLong(yy));
    }

    struct server_object_reference_t* ref = server_map_get_object(
            &obj->server_python->server_state->map, obj->object_id);

    if (ref == NULL)
    {
        Py_RETURN_NONE;
    }

    struct map_object_t* o = &ref->object;
    const uint8_t target_changed = (ref->object.target.x != x) || (ref->object.target.y != y) || (!ref->move_to_target);

    ref->object.target.x = x;
    ref->object.target.y = y;
    ref->move_to_target = 1;

    if (target_changed)
    {
        ref->motion_profile_phase = 0;
        ref->motion_profile_start_distance = object_profile_distance_for_target(ref, x, y);
        ref->motion_profile_target_x = x;
        ref->motion_profile_target_y = y;
    }

    SET_F0(o, OBJECT_F0_DIRTY);

    Py_RETURN_NONE;
}

PyObject* object_api_jump_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    object_obj* obj = (object_obj*)callable;

    struct server_object_reference_t* ref = server_map_get_object(
            &obj->server_python->server_state->map, obj->object_id);

    if (ref == NULL)
    {
        Py_RETURN_NONE;
    }

    struct map_object_t* o = &ref->object;

    if (IS_F1_SET(o, OBJECT_F1_BOTTOM) && (o->speed.y == 0))
    {
        PyObject* power = PyTuple_GetItem(args, 0);
        double power_d = PyFloat_AsDouble(power);

        o->speed.y = (int8_t)(power_d * OBJECT_JUMP_MAXIMUM);
    }

    SET_F0(o, OBJECT_F0_DIRTY);

    Py_RETURN_NONE;
}

PyObject* object_api_set_location_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    object_obj* obj = (object_obj*)callable;

    PyObject* xx = PyTuple_GetItem(args, 0);
    PyObject* yy = PyTuple_GetItem(args, 1);

    uint16_t x;
    uint16_t y;

    if (PyFloat_Check(xx))
    {
        double xd = PyFloat_AsDouble(xx);

        if (server_python_check_err())
        {
            Py_RETURN_NONE;
        }

        x = (uint16_t)xd;
        x = OBJECT_LOGICAL_TO_PHY(x) + (uint16_t)((double)(xd - x) * 8.0f);
    }
    else
    {
        x = OBJECT_LOGICAL_TO_PHY(PyLong_AsLong(xx));
    }

    if (PyFloat_Check(yy))
    {
        double yd = PyFloat_AsDouble(yy);

        if (server_python_check_err())
        {
            Py_RETURN_NONE;
        }

        y = (uint16_t)yd;
        y = OBJECT_LOGICAL_TO_PHY(y) + (uint16_t)((double)(yd - y) * 8.0f);
    }
    else
    {
        y = OBJECT_LOGICAL_TO_PHY(PyLong_AsLong(yy));
    }

    struct server_object_reference_t* ref = server_map_get_object(
            &obj->server_python->server_state->map, obj->object_id);

    if (ref == NULL)
    {
        Py_RETURN_NONE;
    }

    struct map_object_t* o = &ref->object;

    ref->object.target.x = x;
    ref->object.target.y = y;
    ref->object.location.x = x;
    ref->object.location.y = y;
    ref->force_sync = 1;

    SET_F0(o, OBJECT_F0_DIRTY);

    Py_RETURN_NONE;
}

PyObject* object_api_set_speed_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    object_obj* obj = (object_obj*)callable;

    long x = PyLong_AsLong(PyTuple_GetItem(args, 0));
    long y = PyLong_AsLong(PyTuple_GetItem(args, 1));

    struct server_object_reference_t* ref = server_map_get_object(
            &obj->server_python->server_state->map, obj->object_id);

    if (ref == NULL)
    {
        Py_RETURN_NONE;
    }

    struct map_object_t* o = &ref->object;

    ref->object.speed.x = (int8_t)x;
    ref->object.speed.y = (int8_t)y;
    SET_F0(o, OBJECT_F0_DIRTY);

    Py_RETURN_NONE;
}

PyObject* object_api_set_motion_profile_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    object_obj* obj = (object_obj*)callable;

    long profile = PyLong_AsLong(PyTuple_GetItem(args, 0));

    struct server_object_reference_t* ref = server_map_get_object(
            &obj->server_python->server_state->map, obj->object_id);

    if (ref == NULL)
    {
        Py_RETURN_NONE;
    }

    if ((profile < SERVER_OBJECT_MOTION_PROFILE_NONE) || (profile > SERVER_OBJECT_MOTION_PROFILE_ACCELERATING))
    {
        Py_RETURN_NONE;
    }

    if (ref->motion_profile != (uint8_t)profile)
    {
        struct map_object_t* o = &ref->object;
        ref->motion_profile = (uint8_t)profile;
        ref->motion_profile_phase = 0;
        ref->motion_profile_start_distance = object_profile_distance_for_target(
            ref, ref->object.target.x, ref->object.target.y);
        ref->motion_profile_target_x = ref->object.target.x;
        ref->motion_profile_target_y = ref->object.target.y;
        ref->force_sync = 1;
        SET_F0(o, OBJECT_F0_DIRTY);
    }

    Py_RETURN_NONE;
}

PyObject* object_api_set_sprite_offset_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    object_obj* obj = (object_obj*)callable;

    uint16_t sprite_offset = PyLong_AsLong(PyTuple_GetItem(args, 0));

    struct server_object_reference_t* ref = server_map_get_object(
            &obj->server_python->server_state->map, obj->object_id);

    if (ref == NULL)
    {
        Py_RETURN_NONE;
    }

    struct map_object_t* o = &ref->object;

    ref->object.sprite_offset = sprite_offset;
    SET_F0(o, OBJECT_F0_DIRTY);

    Py_RETURN_NONE;
}

PyObject* object_api_looking_left_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    object_obj* obj = (object_obj*)callable;

    struct server_object_reference_t* ref = server_map_get_object(
            &obj->server_python->server_state->map, obj->object_id);

    if (ref == NULL)
    {
        Py_RETURN_FALSE;
    }

    if (ref->object.f0 & OBJECT_F0_LOOKING_LEFT)
    {
        Py_RETURN_TRUE;
    }

    Py_RETURN_FALSE;
}

PyObject* object_api_has_collision_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    object_obj* obj = (object_obj*)callable;

    struct server_object_reference_t* ref = server_map_get_object(
            &obj->server_python->server_state->map, obj->object_id);

    if (ref == NULL)
    {
        Py_RETURN_FALSE;
    }

    const long f1 = PyLong_AsLong(PyTuple_GetItem(args, 0));
    if (ref->object.f1 & f1)
    {
        Py_RETURN_TRUE;
    }

    Py_RETURN_FALSE;
}

PyObject* object_api_get_speed_x_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    object_obj* obj = (object_obj*)callable;

    struct server_object_reference_t* ref = server_map_get_object(
            &obj->server_python->server_state->map, obj->object_id);

    if (ref == NULL)
    {
        return PyLong_FromLong(0);
    }

    return PyLong_FromLong(ref->object.speed.x);
}

PyObject* object_api_get_speed_y_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    object_obj* obj = (object_obj*)callable;

    struct server_object_reference_t* ref = server_map_get_object(
            &obj->server_python->server_state->map, obj->object_id);

    if (ref == NULL)
    {
        return PyLong_FromLong(0);
    }

    return PyLong_FromLong(ref->object.speed.y);
}

PyObject* object_api_get_id_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    object_obj* obj = (object_obj*)callable;

    struct server_object_reference_t* ref = server_map_get_object(
            &obj->server_python->server_state->map, obj->object_id);

    if (ref == NULL)
    {
        return PyLong_FromLong(0);
    }

    return PyLong_FromLong(ref->object.object_id);
}

PyObject* object_api_get_x_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    object_obj* obj = (object_obj*)callable;

    struct server_object_reference_t* ref = server_map_get_object(
            &obj->server_python->server_state->map, obj->object_id);

    if (ref == NULL)
    {
        return PyFloat_FromDouble(0);
    }

    return PyFloat_FromDouble(((double)ref->object.location.x) / 8.0f);
}

PyObject* object_api_get_y_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    object_obj* obj = (object_obj*)callable;

    struct server_object_reference_t* ref = server_map_get_object(
            &obj->server_python->server_state->map, obj->object_id);

    if (ref == NULL)
    {
        return PyFloat_FromDouble(0);
    }

    return PyFloat_FromDouble(((double)ref->object.location.y) / 8.0f);
}
