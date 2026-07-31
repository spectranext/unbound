#include "server_python.h"
#include "server.h"
#include "server_bullets.h"
#include "computer.h"

uint16_t server_pyton_py_to_phy_position_uint16(PyObject* coord);
int8_t server_pyton_py_to_phy_position_int8(PyObject* coord);

PyObject* server_map_api_set_block_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    server_pyton_obj* obj = (server_pyton_obj*)callable;
    uint16_t x = (uint16_t)PyLong_AsLong(PyTuple_GetItem(args, 0));
    uint16_t y = (uint16_t)PyLong_AsLong(PyTuple_GetItem(args, 1));
    PyObject* notify = PyTuple_GetItem(args, 3);

    server_python_map_set_py(obj->server_pyton->server_state,
                             &obj->server_pyton->server_state->map, x, y, PyTuple_GetItem(args, 2),
                             PyObject_IsTrue(notify));

    Py_RETURN_NONE;
}

PyObject* server_map_api_send_effect_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    server_pyton_obj* obj = (server_pyton_obj*)callable;

    PyObject* xx = PyTuple_GetItem(args, 0);
    PyObject* yy = PyTuple_GetItem(args, 1);

    uint16_t x;
    uint16_t y;

    if (PyLong_Check(xx))
    {
        x = OBJECT_LOGICAL_TO_PHY(PyLong_AsLong(xx));
    }
    else
    {
        double xd = PyFloat_AsDouble(xx);
        x = (uint16_t)xd;
        x = OBJECT_LOGICAL_TO_PHY(x) + (uint16_t)((double)(xd - x) * 8.0f);
    }

    if (PyLong_Check(yy))
    {
        y = OBJECT_LOGICAL_TO_PHY(PyLong_AsLong(yy));
    }
    else
    {
        double yd = PyFloat_AsDouble(yy);
        y = (uint16_t)yd;
        y = OBJECT_LOGICAL_TO_PHY(y) + (uint16_t)((double)(yd - y) * 8.0f);
    }

    const char* effect = PyBytes_AsString(PyTuple_GetItem(args, 2));

    map_send_effect(obj->server_pyton->server_state, effect, x, y);
    Py_RETURN_NONE;
}

PyObject* server_map_api_schedule_map_refresh_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    server_pyton_obj* obj = (server_pyton_obj*)callable;
    PyObject* immediate = PyTuple_GetItem(args, 0);
    server_state_schedule_map_refresh(obj->server_pyton->server_state, immediate == Py_True ? 0 : 2000);
    Py_RETURN_NONE;
}

PyObject* server_map_api_schedule_block_method_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    server_pyton_obj* obj = (server_pyton_obj*)callable;

    uint16_t x = (uint16_t)PyLong_AsLong(PyTuple_GetItem(args, 0));
    uint16_t y = (uint16_t)PyLong_AsLong(PyTuple_GetItem(args, 1));
    uint16_t time = (uint16_t)PyLong_AsLong(PyTuple_GetItem(args, 2));
    const char* method = PyBytes_AS_STRING(PyTuple_GetItem(args, 3));

    server_state_schedule_block_method(obj->server_pyton->server_state, x, y, time, method);
    Py_RETURN_NONE;
}

PyObject* server_map_api_schedule_callback_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    server_pyton_obj* obj = (server_pyton_obj*)callable;

    PyObject* cb = PyTuple_GetItem(args, 0);
    uint16_t time = (uint16_t)PyLong_AsLong(PyTuple_GetItem(args, 1));

    server_state_schedule_callback(obj->server_pyton->server_state, cb, time);
    Py_RETURN_NONE;
}

PyObject* server_map_api_update_block_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    server_pyton_obj* obj = (server_pyton_obj*)callable;
    uint16_t x = (uint16_t)PyLong_AsLong(PyTuple_GetItem(args, 0));
    uint16_t y = (uint16_t)PyLong_AsLong(PyTuple_GetItem(args, 1));
    PyObject* notify = PyTuple_GetItem(args, 2);

    if (PyObject_IsTrue(notify))
    {
        // update is going to call refresh too
        server_notify_block_update(obj->server_pyton->server_state, &obj->server_pyton->server_state->map, x, y);
    }
    else
    {
        server_python_map_refresh_block_code(&obj->server_pyton->server_state->map.map, x, y, 1);
    }

    Py_RETURN_NONE;
}

PyObject* server_map_api_query_objects_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    server_pyton_obj* obj = (server_pyton_obj*)callable;
    uint16_t x = (uint16_t)PyLong_AsLong(PyTuple_GetItem(args, 0));
    uint16_t y = (uint16_t)PyLong_AsLong(PyTuple_GetItem(args, 1));
    uint16_t w = (uint16_t)PyLong_AsLong(PyTuple_GetItem(args, 2));
    uint16_t h = (uint16_t)PyLong_AsLong(PyTuple_GetItem(args, 3));

    struct server_state_t* server_state = obj->server_pyton->server_state;

    Py_ssize_t sz = 0;

    struct server_object_reference_t* object;
    struct server_object_reference_t* tmp;
    HASH_ITER(hh, server_state->map.objects, object, tmp)
    {
        if (object->py_object == NULL)
            continue;
        uint16_t lx = OBJECT_PHY_TO_LOGICAL(object->object.location.x);
        uint16_t ly = OBJECT_PHY_TO_LOGICAL(object->object.location.y);
        if (lx < x || lx > x + w)
            continue;
        if (ly < y || ly > y + h)
            continue;
        sz++;
    }

    PyObject* list = PyList_New(sz);

    Py_ssize_t index = 0;

    HASH_ITER(hh, server_state->map.objects, object, tmp)
    {
        if (object->py_object == NULL)
            continue;
        uint16_t lx = OBJECT_PHY_TO_LOGICAL(object->object.location.x);
        uint16_t ly = OBJECT_PHY_TO_LOGICAL(object->object.location.y);
        if (lx < x || lx > x + w)
            continue;
        if (ly < y || ly > y + h)
            continue;

        Py_IncRef(object->py_object);
        PyList_SetItem(list, index++, object->py_object);
    }

    return list;
}

PyObject* server_map_api_get_object_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    server_pyton_obj* obj = (server_pyton_obj*)callable;
    uint16_t object_id = (uint16_t)PyLong_AsLong(PyTuple_GetItem(args, 0));
    struct server_state_t* server_state = obj->server_pyton->server_state;
    struct server_object_reference_t *object = server_map_get_object(&server_state->map, object_id);
    if (object == NULL)
    {
        Py_RETURN_NONE;
    }

    if (object->py_object == NULL)
    {
        Py_RETURN_NONE;
    }

    Py_IncRef(object->py_object);
    return object->py_object;
}

PyObject* server_map_api_query_clients_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    server_pyton_obj* obj = (server_pyton_obj*)callable;

    struct server_state_t* server_state = obj->server_pyton->server_state;

    Py_ssize_t sz = 0;

    struct client_state_t* object;
    struct client_state_t* tmp;
    LL_FOREACH_SAFE(server_state->client_states, object, tmp)
    {
        if (object->client_id == 0)
            continue;
        sz++;
    }

    PyObject* list = PyList_New(sz);

    Py_ssize_t index = 0;

    LL_FOREACH_SAFE(server_state->client_states, object, tmp)
    {
        if (object->client_id == 0)
            continue;
        Py_IncRef(object->py);
        PyList_SetItem(list, index++, object->py);
    }

    return list;
}

PyObject* server_map_api_get_client_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    server_pyton_obj* obj = (server_pyton_obj*)callable;
    uint16_t client_id = (uint16_t)PyLong_AsLong(PyTuple_GetItem(args, 0));

    struct server_state_t* server_state = obj->server_pyton->server_state;
    struct client_state_t* client_state = client_state_find_id(server_state, client_id);
    if (client_state == NULL)
    {
        Py_RETURN_NONE;
    }

    Py_IncRef(client_state->py);
    return client_state->py;
}

PyObject* server_map_api_query_team_computers_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    server_pyton_obj* obj = (server_pyton_obj*)callable;
    uint16_t team_id = (uint16_t)PyLong_AsLong(PyTuple_GetItem(args, 0));

    struct server_state_t* server_state = obj->server_pyton->server_state;

    Py_ssize_t sz = 0;
    struct computer_t* computer;
    struct computer_t* tmp;

    HASH_ITER(hh, server_state->computers, computer, tmp)
    {
        if (computer->device.namespace_id != team_id)
            continue;
        if (computer->computer_api == NULL)
            continue;
        sz++;
    }

    PyObject* list = PyList_New(sz);
    Py_ssize_t index = 0;

    HASH_ITER(hh, server_state->computers, computer, tmp)
    {
        if (computer->device.namespace_id != team_id)
            continue;
        if (computer->computer_api == NULL)
            continue;

        Py_IncRef(computer->computer_api);
        PyList_SetItem(list, index++, computer->computer_api);
    }

    return list;
}

PyObject* server_map_api_spawn_player_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    server_pyton_obj* obj = (server_pyton_obj*)callable;
    PyObject* py = PyTuple_GetItem(args, 0);
    uint16_t xx = server_pyton_py_to_phy_position_uint16(PyTuple_GetItem(args, 1));
    uint16_t yy = server_pyton_py_to_phy_position_uint16(PyTuple_GetItem(args, 2));

    uint16_t client_id = (uint16_t)PyLong_AsLong(PyObject_GetAttrString(py, "client_id"));

    struct client_state_t* client_state = client_state_find_id(obj->server_pyton->server_state, client_id);
    if (client_state == NULL)
    {
        Py_RETURN_NONE;
    }

    client_printf(client_state, "spawning client on %dx%d\n", OBJECT_PHY_TO_LOGICAL(xx), OBJECT_PHY_TO_LOGICAL(yy));
    PyObject* py_object = server_python_allocate_py_player(obj->server_pyton, client_id, py);
    if (py_object == NULL)
    {
        client_printf(client_state, "cannot allocate player py\n");
        Py_RETURN_NONE;
    }

    struct server_object_reference_t* new_player = map_add_object(
        obj->server_pyton->server_state,
        &obj->server_pyton->server_state->map.map, xx, yy,
        0xFFFF);

    if (new_player == NULL)
    {
        PyErr_SetString(PyExc_RuntimeError, "Cannot spawn a new player");
        return NULL;
    }

    client_state->client_object = new_player->object.object_id;

    strncpy(new_player->name, client_state->user_name, sizeof(new_player->name));

    map_object_assign_py(obj->server_pyton->server_state, new_player, py_object);
    map_object_finalize(obj->server_pyton->server_state, new_player);

    server_state_client_sync_stats(client_state, obj->server_pyton->server_state);

    new_player->object.client_id = client_id;
    new_player->client_id = client_id;

    Py_INCREF(new_player->py_object);

    return new_player->py_object;
}

PyObject* server_map_api_spawn_object_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    server_pyton_obj* obj = (server_pyton_obj*)callable;
    uint16_t x = (uint16_t)server_pyton_py_to_phy_position_uint16(PyTuple_GetItem(args, 0));
    uint16_t y = (uint16_t)server_pyton_py_to_phy_position_uint16(PyTuple_GetItem(args, 1));
    const char* kind = PyBytes_AsString(PyTuple_GetItem(args, 2));

    PyObject* py = server_python_allocate_py_object(obj->server_pyton, kind);
    if (py == NULL)
    {
        PyErr_SetString(PyExc_RuntimeError, "Cannot allocate bot");
        return NULL;
    }

    struct server_object_reference_t* new_player = map_add_object(
            obj->server_pyton->server_state,
            &obj->server_pyton->server_state->map.map, x, y,
            0xFFFF);

    if (new_player == NULL)
    {
        PyErr_SetString(PyExc_RuntimeError, "Cannot spawn a new bot");
        return NULL;
    }

    map_object_assign_py(obj->server_pyton->server_state, new_player, py);
    map_object_finalize(obj->server_pyton->server_state, new_player);

    new_player->object.payload = 0;

    Py_IncRef(new_player->py_object);

    return new_player->py_object;
}

PyObject* server_map_api_iterate_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    server_pyton_obj* obj = (server_pyton_obj*)callable;
    uint16_t from_x = (uint16_t)PyLong_AsLong(PyTuple_GetItem(args, 0));
    uint16_t from_y = (uint16_t)PyLong_AsLong(PyTuple_GetItem(args, 1));
    uint16_t to_x = (uint16_t)PyLong_AsLong(PyTuple_GetItem(args, 2));
    uint16_t to_y = (uint16_t)PyLong_AsLong(PyTuple_GetItem(args, 3));
    PyObject* cb = PyTuple_GetItem(args, 4);

    for (uint16_t y = from_y; y < to_y; y++)
    {
        for (uint16_t x = from_x; x < to_x; x++)
        {
            struct block_metadata_t* block_object =
                    server_map_get_block_metadata(&obj->server_pyton->server_state->map.map, x, y);

            if (block_object->py_object == NULL)
            {
                PyObject_CallFunction(cb, "O", Py_None);
            }
            else
            {
                PyObject_CallFunction(cb, "O", block_object->py_object);
            }

            server_python_check_err();
        }
    }

    Py_RETURN_NONE;
}

PyObject* server_map_api_get_block_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    server_pyton_obj* obj = (server_pyton_obj*)callable;
    uint16_t x = (uint16_t)PyLong_AsLong(PyTuple_GetItem(args, 0));
    uint16_t y = (uint16_t)PyLong_AsLong(PyTuple_GetItem(args, 1));

    struct map_t* map = &obj->server_pyton->server_state->map.map;

    if (x >= (map->width * MAP_CHUNK_SIZE) ||
        y >= (map->height * MAP_CHUNK_SIZE))
    {
        Py_RETURN_NONE;
    }

    struct block_metadata_t* block_object =
            server_map_get_block_metadata(&obj->server_pyton->server_state->map.map, x, y);

    if (block_object->py_object == NULL)
    {
        Py_RETURN_NONE;
    }

    Py_IncRef(block_object->py_object);
    return block_object->py_object;
}

PyObject* server_map_api_set_new_client_callback_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    server_pyton_obj* obj = (server_pyton_obj*)callable;
    obj->server_pyton->new_client_callback = PyTuple_GetItem(args, 0);
    Py_IncRef(obj->server_pyton->new_client_callback);

    server_printf("Assigned spawn callback\n");

    Py_RETURN_NONE;
}

PyObject* server_map_api_get_width_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    server_pyton_obj* obj = (server_pyton_obj*)callable;
    return Py_BuildValue("i", obj->server_pyton->server_state->map.map.width * MAP_CHUNK_SIZE);
}

PyObject* server_map_api_get_height_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    server_pyton_obj* obj = (server_pyton_obj*)callable;
    return Py_BuildValue("i", obj->server_pyton->server_state->map.map.height * MAP_CHUNK_SIZE);
}

PyObject* server_map_api_shutdown_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    server_pyton_obj* obj = (server_pyton_obj*)callable;
    obj->server_pyton->server_state->running = 0;
    Py_RETURN_NONE;
}


uint16_t server_pyton_py_to_phy_position_uint16(PyObject* coord)
{
    if (PyFloat_Check(coord))
    {
        double xd = PyFloat_AsDouble(coord);

        if (server_python_check_err())
        {
            return 0;
        }

        int x = (int)xd;
        return OBJECT_LOGICAL_TO_PHY(x) + (int)((double)(xd - x) * 8.0f);
    }
    else
    {
        return OBJECT_LOGICAL_TO_PHY(PyLong_AsLong(coord));
    }
}

int8_t server_pyton_py_to_phy_position_int8(PyObject* coord)
{
    if (PyFloat_Check(coord))
    {
        double xd = PyFloat_AsDouble(coord);

        if (server_python_check_err())
        {
            return 0;
        }

        int x = (int)xd;
        return OBJECT_LOGICAL_TO_PHY(x) + (int)((double)(xd - x) * 8.0f);
    }
    else
    {
        return OBJECT_LOGICAL_TO_PHY(PyLong_AsLong(coord));
    }
}

PyObject* server_map_api_add_bullet_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    server_pyton_obj *obj = (server_pyton_obj *) callable;

    uint16_t x = server_pyton_py_to_phy_position_uint16(PyTuple_GetItem(args, 0));
    uint16_t y = server_pyton_py_to_phy_position_uint16(PyTuple_GetItem(args, 1));
    uint16_t team_id = PyLong_AsLong(PyTuple_GetItem(args, 2));
    uint16_t damage = PyLong_AsLong(PyTuple_GetItem(args, 3));
    uint16_t angle = (uint16_t)PyLong_AsLong(PyTuple_GetItem(args, 4));

    PyObject* sound = PyTuple_GetItem(args, 5);
    uint8_t _sound;
    if (sound == Py_None)
    {
        _sound = 0xFF;
    }
    else
    {
        _sound = PyLong_AsLong(sound);
    }

    PyObject* effect = PyTuple_GetItem(args, 6);
    char* _effect;
    if (effect == Py_None)
    {
        _effect = NULL;
    }
    else
    {
        _effect = PyBytes_AsString(effect);
    }

    server_bullets_add(obj->server_pyton->server_state, x, y, team_id, damage, angle, _sound, _effect);
    Py_RETURN_NONE;
}
