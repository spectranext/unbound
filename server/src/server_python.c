#include "server_python.h"
#include "server.h"
#include "utils.h"
#include "computer.h"

#pragma clang diagnostic push
#pragma ide diagnostic ignored "UnusedParameter"

uint8_t server_python_check_err()
{
    if (PyErr_Occurred())
    {
        PyErr_Print();

        char *error = "Server error occurred.";
        extern struct server_state_t server_state;

        struct client_state_t *client_state;
        LL_FOREACH(server_state.client_states, client_state) {
            client_state_notify_message(&server_state, client_state, error, NOTIFY_MESSAGE_COLOR_DANGER);
        }

        PyErr_Clear();
        return 1;
    }
    return 0;
}

#define PY_CALL_METHOD_DISCARD(py_obj, method_name, fmt, ...)                            \
    do                                                                                   \
    {                                                                                    \
        PyObject* _py_call_target = (py_obj);                                            \
        if (_py_call_target)                                                              \
        {                                                                                \
            Py_IncRef(_py_call_target);                                                  \
            PyObject* _py_call_result = PyObject_CallMethod(                             \
                _py_call_target, (method_name), (fmt), ##__VA_ARGS__);                   \
            Py_XDECREF(_py_call_result);                                                 \
            Py_DecRef(_py_call_target);                                                  \
        }                                                                                \
    } while (0)

#define PY_CALL_FUNCTION_DISCARD(py_fn, fmt, ...)                                        \
    do                                                                                   \
    {                                                                                    \
        PyObject* _py_call_fn = (py_fn);                                                 \
        if (_py_call_fn)                                                                  \
        {                                                                                \
            Py_IncRef(_py_call_fn);                                                      \
            PyObject* _py_call_result = PyObject_CallFunction(                           \
                _py_call_fn, (fmt), ##__VA_ARGS__);                                      \
            Py_XDECREF(_py_call_result);                                                 \
            Py_DecRef(_py_call_fn);                                                      \
        }                                                                                \
    } while (0)

block_t server_python_map_refresh_block_code(struct map_t* map, uint16_t x, uint16_t y,
    uint8_t call_refresh)
{
    struct block_metadata_t* block_object =
        server_map_get_block_metadata(map, x, y);

    if (block_object->py_object == NULL)
    {
        map_set_block(map, x, y, 0, 0);
        return 0;
    }
    else
    {
        if (call_refresh)
        {
            PY_CALL_METHOD_DISCARD(block_object->py_object, "refresh", "");
            server_python_check_err();
        }

        PyObject* code = PyObject_CallMethod(block_object->py_object, "get_code", "");

        if (server_python_check_err())
        {
            return 0;
        }

        PyObject* light = PyObject_CallMethod(block_object->py_object, "get_light", "");

        if (server_python_check_err())
        {
            return 0;
        }

        block_t code_as_long = PyLong_AsLong(code);
        block_t light_as_long = PyLong_AsLong(light);
        Py_DecRef(code);
        Py_DecRef(light);

        map_set_block(map, x, y, code_as_long, light_as_long);

        return code_as_long;
    }
}

uint8_t server_python_map_call(PyObject* cb)
{
    PY_CALL_FUNCTION_DISCARD(cb, "");

    if (server_python_check_err())
    {
        Py_DecRef(cb);
        return 1;
    }

    Py_DecRef(cb);
    return 0;
}

uint8_t server_python_map_call_block_method(struct map_t* map, uint16_t x, uint16_t y, const char* method)
{
    struct block_metadata_t* block_object =
        server_map_get_block_metadata(map, x, y);

    if (block_object->py_object == NULL)
    {
        return 1;
    }

    PY_CALL_METHOD_DISCARD(block_object->py_object, method, "");

    if (server_python_check_err())
    {
        return 1;
    }

    return 0;
}

void server_python_map_call_block_overlap(struct map_t* map, uint16_t x, uint16_t y,
    struct server_object_reference_t* ref)
{
    if (ref->py_object == NULL)
    {
        return;
    }

    struct block_metadata_t* block_object =
        server_map_get_block_metadata(map, x, y);

    if (block_object == NULL || block_object->py_object == NULL)
    {
        return;
    }

    PY_CALL_METHOD_DISCARD(block_object->py_object, "on_overlap", "O", ref->py_object);
    server_python_check_err();
}

PyObject* server_printf_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    wchar_t* s = PyUnicode_AsWideCharString(PyTuple_GetItem(args, 0), NULL);
    python_printf("%ls\n", s);
    PyMem_Free(s);
    Py_RETURN_NONE;
}

uint8_t server_python_init(struct server_state_t* state, struct server_python_t* server_python,
    const char* debug_host, int debug_port)
{
    memset(server_python, 0, sizeof(struct server_python_t));

    server_python->server_state = state;

    Py_Initialize();

    {
        PyObject* sys = PyImport_ImportModule("sys");
        PyObject* sys_dict = PyModule_GetDict(sys);
        PyObject* sys_path = PyDict_GetItemString(sys_dict, "path");

        PyObject* python_path = PyUnicode_FromString((char*) "./python");
        PyList_Append(sys_path, python_path);
        Py_DecRef(python_path);
        Py_DecRef(sys);
    }

    if (debug_host && debug_port)
    {
        PyObject* pydevd_pycharm = PyImport_ImportModule("pydevd_pycharm");
        if (server_python_check_err())
        {
            server_printf("Can't host debugging information\n");
        }
        else
        {
            PyObject* pydevd_pycharm_dict = PyModule_GetDict(pydevd_pycharm);
            PyObject* pydevd_pycharm_settrace = PyDict_GetItemString(pydevd_pycharm_dict, "settrace");

            PY_CALL_FUNCTION_DISCARD(pydevd_pycharm_settrace, "sOOiO", debug_host, Py_True, Py_True, debug_port, Py_False);
            if (server_python_check_err())
            {
                server_printf("Can't host debugging information\n");
            }
        }
        Py_XDECREF(pydevd_pycharm);
    }

    PyObject* module_name = PyUnicode_FromString((char*) "server");
    if (server_python_check_err())
    {
        return 2;
    }

    server_python->module = PyImport_Import(module_name);
    Py_DECREF(module_name);

    if (server_python_check_err())
    {
        return 3;
    }

    PyObject* module_dict = PyModule_GetDict(server_python->module);
    PyObject* MapAPI_create = PyDict_GetItemString(module_dict, "create_map");

    server_python->update_map_call = PyDict_GetItemString(module_dict, "update_map");
    Py_IncRef(server_python->update_map_call);
    server_python->map_api = PyObject_CallFunction(MapAPI_create, "");
    if (server_python_check_err())
    {
        return 4;
    }

    ASSIGN_SERVER_CALLBACK(server_python->map_api, "print", server_print_Type, server_pyton_obj)
    ASSIGN_SERVER_CALLBACK(server_python->map_api, "set_block", server_map_api_set_block_Type, server_pyton_obj)
    ASSIGN_SERVER_CALLBACK(server_python->map_api, "update_block", server_map_api_update_block_Type, server_pyton_obj)
    ASSIGN_SERVER_CALLBACK(server_python->map_api, "get_block", server_map_api_get_block_Type, server_pyton_obj)
    ASSIGN_SERVER_CALLBACK(server_python->map_api, "get_width", server_map_api_get_width_Type, server_pyton_obj)
    ASSIGN_SERVER_CALLBACK(server_python->map_api, "get_height", server_map_api_get_height_Type, server_pyton_obj)
    ASSIGN_SERVER_CALLBACK(server_python->map_api, "shutdown", server_map_api_shutdown_Type, server_pyton_obj)
    ASSIGN_SERVER_CALLBACK(server_python->map_api, "set_new_client_callback", server_map_api_set_new_client_callback_Type, server_pyton_obj)
    ASSIGN_SERVER_CALLBACK(server_python->map_api, "iterate", server_map_api_iterate_Type, server_pyton_obj)
    ASSIGN_SERVER_CALLBACK(server_python->map_api, "schedule_map_refresh", server_map_api_schedule_map_refresh_Type, server_pyton_obj)
    ASSIGN_SERVER_CALLBACK(server_python->map_api, "schedule_block_method", server_map_api_schedule_block_method_Type, server_pyton_obj)
    ASSIGN_SERVER_CALLBACK(server_python->map_api, "schedule_callback", server_map_api_schedule_callback_Type, server_pyton_obj)
    ASSIGN_SERVER_CALLBACK(server_python->map_api, "send_effect", server_map_api_send_effect_Type, server_pyton_obj)
    ASSIGN_SERVER_CALLBACK(server_python->map_api, "spawn_object", server_map_api_spawn_object_Type, server_pyton_obj)
    ASSIGN_SERVER_CALLBACK(server_python->map_api, "spawn_player", server_map_api_spawn_player_Type, server_pyton_obj)
    ASSIGN_SERVER_CALLBACK(server_python->map_api, "query_objects", server_map_api_query_objects_Type, server_pyton_obj)
    ASSIGN_SERVER_CALLBACK(server_python->map_api, "get_object", server_map_api_get_object_Type, server_pyton_obj)
    ASSIGN_SERVER_CALLBACK(server_python->map_api, "query_clients", server_map_api_query_clients_Type, server_pyton_obj)
    ASSIGN_SERVER_CALLBACK(server_python->map_api, "get_client", server_map_api_get_client_Type, server_pyton_obj)
    ASSIGN_SERVER_CALLBACK(server_python->map_api, "query_team_computers", server_map_api_query_team_computers_Type, server_pyton_obj)
    ASSIGN_SERVER_CALLBACK(server_python->map_api, "computer_new", server_map_api_computer_new_Type, server_pyton_obj)
    ASSIGN_SERVER_CALLBACK(server_python->map_api, "computer_find", server_map_api_computer_find_Type, server_pyton_obj)
    ASSIGN_SERVER_CALLBACK(server_python->map_api, "device_new", server_map_api_device_new_Type, server_pyton_obj)
    ASSIGN_SERVER_CALLBACK(server_python->map_api, "add_bullet", server_map_api_add_bullet_Type, server_pyton_obj)

    {
        server_python->block_spawn = PyDict_GetItemString(module_dict, "create_block_str");
        Py_IncRef(server_python->block_spawn);
    }

    server_python->computer_api = PyDict_GetItemString(module_dict, "ComputerAPI");
    Py_IncRef(server_python->computer_api);

    server_python->device_api = PyDict_GetItemString(module_dict, "DeviceAPI");
    Py_IncRef(server_python->device_api);

    return 0;
}

void server_python_map_generate(struct server_python_t* server_python, const char* scenario)
{
    PyObject* module_dict = PyModule_GetDict(server_python->module);

    PyObject* generate_fn = PyDict_GetItemString(module_dict, "generate_map");

    if (server_python_check_err())
    {
        return;
    }

    PY_CALL_FUNCTION_DISCARD(generate_fn, "Oy", server_python->map_api, scenario);
    server_python_check_err();
}

void server_python_map_init(struct server_python_t* server_python, const char* scenario)
{
    PyObject* module_dict = PyModule_GetDict(server_python->module);

    PyObject* generate_fn = PyDict_GetItemString(module_dict, "init_map");

    if (server_python_check_err())
    {
        return;
    }

    PY_CALL_FUNCTION_DISCARD(generate_fn, "Oy", server_python->map_api, scenario);
    server_python_check_err();
}

void server_python_map_update(struct server_python_t* server_python)
{
    PY_CALL_FUNCTION_DISCARD(server_python->update_map_call, "O", server_python->map_api);
    server_python_check_err();
}

void server_python_map_shutdown(struct server_python_t* server_python)
{
    PyObject* module_dict = PyModule_GetDict(server_python->module);
    PyObject* generate_fn = PyDict_GetItemString(module_dict, "shutdown_map");

    if (server_python_check_err())
    {
        return;
    }

    PY_CALL_FUNCTION_DISCARD(generate_fn, "O", server_python->map_api);
    server_python_check_err();
}

void server_python_map_on_chat_message(struct server_python_t* server_python,
    struct client_state_t* client_state, const char* message)
{
    PyObject* author = client_state ? client_state->py : Py_None;
    Py_IncRef(author);

    PyObject* result = PyObject_CallMethod(server_python->map_api, "on_chat_message", "Oy", author, message);
    Py_XDECREF(result);
    Py_DecRef(author);
    server_python_check_err();
}

uint8_t server_python_map_refresh(struct server_python_t* server_python)
{
    PyObject* module_dict = PyModule_GetDict(server_python->module);

    PyObject* generate_fn = PyDict_GetItemString(module_dict, "refresh_map");

    if (server_python_check_err())
    {
        return 0;
    }

    PyObject* res = PyObject_CallFunction(generate_fn, "O", server_python->map_api);

    server_python_check_err();

    uint8_t result = (res == Py_True) ? 1 : 0;
    Py_XDECREF(res);

    return result;
}

void server_python_map_yield_chunk(struct server_state_t* server_state,
    struct server_python_t* server_python, uint16_t ix, uint16_t iy)
{
    for (uint8_t y = 0; y < MAP_CHUNK_SIZE; y++)
    {
        for (uint8_t x = 0; x < MAP_CHUNK_SIZE; x++)
        {
            server_python_map_refresh_block_code(
                &server_python->server_state->map.map,
                ix * MAP_CHUNK_SIZE + x, iy * MAP_CHUNK_SIZE + y, 0);
        }
    }
}

extern void server_python_map_yield_chunks(struct server_state_t* server_state,
    struct server_python_t* server_python)
{
    for (uint16_t j = 0; j < server_python->server_state->map.map.height; j++)
    {
        for (uint16_t i = 0; i < server_python->server_state->map.map.width; i++)
        {
            server_python_map_yield_chunk(server_state, server_python, i, j);
        }
    }
}

void server_python_free(struct server_python_t* server_python)
{
    Py_Finalize();
}

void server_python_assign_py_callbacks(struct server_python_t* server_python, PyObject* py, struct server_object_reference_t* ref)
{
    uint16_t object_id = ref->object.object_id;

    ASSIGN_OBJECT_CALLBACK(object_id, server_python, py, "looking_left", object_api_looking_left_Type, object_obj);
    ASSIGN_OBJECT_CALLBACK(object_id, server_python, py, "has_collision", object_api_has_collision_Type, object_obj);
    ASSIGN_OBJECT_CALLBACK(object_id, server_python, py, "get_x", object_api_get_x_Type, object_obj);
    ASSIGN_OBJECT_CALLBACK(object_id, server_python, py, "get_y", object_api_get_y_Type, object_obj);
    ASSIGN_OBJECT_CALLBACK(object_id, server_python, py, "get_id", object_api_get_id_Type, object_obj);
    ASSIGN_OBJECT_CALLBACK(object_id, server_python, py, "get_speed_x", object_api_get_speed_x_Type, object_obj);
    ASSIGN_OBJECT_CALLBACK(object_id, server_python, py, "get_speed_y", object_api_get_speed_y_Type, object_obj);
    ASSIGN_OBJECT_CALLBACK(object_id, server_python, py, "set_speed", object_api_set_speed_Type, object_obj);
    ASSIGN_OBJECT_CALLBACK(object_id, server_python, py, "move_to", object_api_move_to_Type, object_obj);
    ASSIGN_OBJECT_CALLBACK(object_id, server_python, py, "jump", object_api_jump_Type, object_obj);
    ASSIGN_OBJECT_CALLBACK(object_id, server_python, py, "set_location", object_api_set_location_Type, object_obj);
    ASSIGN_OBJECT_CALLBACK(object_id, server_python, py, "set_sprite_offset", object_api_set_sprite_offset_Type, object_obj);
    ASSIGN_OBJECT_CALLBACK(object_id, server_python, py, "set_motion_profile", object_api_set_motion_profile_Type, object_obj);
    ASSIGN_OBJECT_CALLBACK(object_id, server_python, py, "set_state_flags", object_api_set_state_flags_Type, object_obj);
    ASSIGN_OBJECT_CALLBACK(object_id, server_python, py, "set_object_state", object_api_set_object_state_Type, object_obj);
    ASSIGN_OBJECT_CALLBACK(object_id, server_python, py, "reset_object_state", object_api_reset_object_state_Type, object_obj);
    ASSIGN_OBJECT_CALLBACK(object_id, server_python, py, "destroy", object_api_destroy_Type, object_obj);
}

void server_python_player_resume(struct server_python_t* server_python, struct client_state_t* client_state,
    struct server_object_reference_t* ref)
{
    PY_CALL_METHOD_DISCARD(client_state->py, "resume_player", "O", ref->py_object);

    server_python_check_err();
}

uint8_t server_python_allocate_client(struct server_python_t* server_python, uint16_t client_id)
{
    struct client_state_t* client_state = client_state_find_id(server_python->server_state, client_id);
    if (client_state == NULL)
        return 1;

    PyObject* module_dict = PyModule_GetDict(server_python->module);
    PyObject* allocate_client = PyDict_GetItemString(module_dict, "allocate_client");

    if (server_python_check_err())
    {
        return 2;
    }

    PyObject* player = PyObject_CallFunction(allocate_client, "i", client_id);

    if (server_python_check_err())
    {
        return 3;
    }

    client_state->py = player;
    Py_IncRef(player);

    client_state->client_id = client_id;
    return 0;
}

void server_python_on_new_client(struct server_python_t* server_python, uint16_t client_id, const char* scenario)
{
    struct client_state_t* client_state = client_state_find_id(server_python->server_state, client_id);
    if (client_state == NULL)
        return;

    PY_CALL_FUNCTION_DISCARD(server_python->new_client_callback, "Oy", client_state->py, scenario);

    server_python_check_err();
}

PyObject* server_python_allocate_py_player(struct server_python_t* server_python, uint16_t client_id, PyObject* py)
{
    PyObject* module_dict = PyModule_GetDict(server_python->module);
    PyObject* allocate_player = PyDict_GetItemString(module_dict, "allocate_player");

    if (server_python_check_err())
    {
        return NULL;
    }

    PyObject* player = PyObject_CallFunction(allocate_player, "iO", client_id, py);

    if (server_python_check_err())
    {
        return NULL;
    }

    python_printf("allocated py object for player %d\n", client_id);

    return player;
}

void server_python_assign_py_player_callbacks(struct server_python_t* server_python, struct client_state_t* client_state)
{
    ASSIGN_CLIENT_CALLBACK(client_state, "notify", client_api_notify_Type, client_pyton_obj);
    ASSIGN_CLIENT_CALLBACK(client_state, "disconnect", client_api_disconnect_Type, client_pyton_obj);
    ASSIGN_CLIENT_CALLBACK(client_state, "sync_stats", client_api_sync_stats_Type, client_pyton_obj);
    ASSIGN_CLIENT_CALLBACK(client_state, "send_chat_message", client_api_send_chat_message_Type, client_pyton_obj);
    ASSIGN_CLIENT_CALLBACK(client_state, "push_module", client_api_push_module_Type, client_pyton_obj);
    ASSIGN_CLIENT_CALLBACK(client_state, "list_modules", client_api_list_modules_Type, client_pyton_obj);
    ASSIGN_CLIENT_CALLBACK(client_state, "get_module_prop_int", client_api_get_module_prop_int_Type, client_pyton_obj);
    ASSIGN_CLIENT_CALLBACK(client_state, "get_module_prop_str", client_api_get_module_prop_str_Type, client_pyton_obj);
    ASSIGN_CLIENT_CALLBACK(client_state, "push_screen", client_api_push_screen_Type, client_pyton_obj);
    ASSIGN_CLIENT_CALLBACK(client_state, "push_memory", client_api_push_memory_Type, client_pyton_obj);
    ASSIGN_CLIENT_CALLBACK(client_state, "module_action", client_api_module_action_Type, client_pyton_obj);
    ASSIGN_CLIENT_CALLBACK(client_state, "force_watch", client_api_force_watch_Type, client_pyton_obj);
    ASSIGN_CLIENT_CALLBACK(client_state, "get_name", client_api_get_name_Type, client_pyton_obj);
    ASSIGN_CLIENT_CALLBACK(client_state, "get_user_id", client_api_get_user_id_Type, client_pyton_obj);
    ASSIGN_CLIENT_CALLBACK(client_state, "set_object_control", client_set_object_control_Type, client_pyton_obj);
    ASSIGN_CLIENT_CALLBACK(client_state, "on_team_set", client_on_team_set_Type, client_pyton_obj);
    ASSIGN_CLIENT_CALLBACK(client_state, "get_client_object_id", client_get_client_object_id_Type, client_pyton_obj);
    ASSIGN_CLIENT_CALLBACK(client_state, "get_control_object_id", client_get_control_object_id_Type, client_pyton_obj);
}

void server_python_set_client_authenticated(struct server_python_t* server_python, struct client_state_t* client_state)
{
    PyObject_SetAttrString(client_state->py, "authenticated", Py_True);
}

extern uint8_t server_python_client_auth(struct server_python_t* server_python,
    const char* token, struct client_auth_result_t* result)
{
    PyObject* module_dict = PyModule_GetDict(server_python->module);
    PyObject* auth_client = PyDict_GetItemString(module_dict, "auth_client");

    PyObject* res = PyObject_CallFunction(auth_client, "y", token);

    if (server_python_check_err())
    {
        return 1;
    }

    if (res == Py_None)
    {
        Py_DecRef(res);
        return 2;
    }

    if (PyBytes_Check(res))
    {
        // error
        strcpy(result->error, PyBytes_AsString(res));
        Py_DecRef(res);
        return 3;
    }
    else
    {
        strcpy(result->user_id, PyBytes_AsString(PyObject_GetAttrString(res, "user_id")));
        strcpy(result->user_name, PyBytes_AsString(PyObject_GetAttrString(res, "user_name")));
        strcpy(result->token, PyBytes_AsString(PyObject_GetAttrString(res, "token")));
    }

    Py_DecRef(res);

    return 0;
}

PyObject* server_python_allocate_py_object(struct server_python_t* server_python, const char* kind)
{
    PyObject* module_dict = PyModule_GetDict(server_python->module);
    PyObject* allocate_object = PyDict_GetItemString(module_dict, "allocate_object");

    if (server_python_check_err())
    {
        return NULL;
    }

    PyObject* py_kind = PyBytes_FromString(kind);

    PyObject* object = PyObject_CallFunction(
        allocate_object, "OO", server_python->map_api, py_kind);

    Py_DecRef(py_kind);

    if (object == Py_None)
    {
        Py_DecRef(object);
        return NULL;
    }

    if (server_python_check_err())
    {
        return NULL;
    }

    python_printf("allocated py object for bot %s\n", kind);

    return object;
}

map_object_type_t server_python_object_py_get_type(struct server_python_t* server_python, struct server_object_reference_t* ref)
{
    if (ref->py_object)
    {
        PyObject* object_type = PyObject_GetAttrString(ref->py_object, "object_type");

        if (server_python_check_err())
        {
            return 0;
        }

        long v = PyLong_AsLong(object_type);
        Py_DecRef(object_type);
        return v;
    }
    else
    {
        return 0;
    }
}

const char* server_python_object_py_get_data_entry(struct server_python_t* server_python, struct server_object_reference_t* ref,
    const char* kind)
{
    if (ref->py_object)
    {
        PyObject* data_entry = PyObject_GetAttrString(ref->py_object, kind);

        if (server_python_check_err())
        {
            return NULL;
        }

        if (Py_IsNone(data_entry))
        {
            return NULL;
        }

        return PyBytes_AsString(data_entry);
    }
    else
    {
        return NULL;
    }
}

void server_python_init_object_py(struct server_python_t* server_python, struct server_object_reference_t* ref)
{
    if (ref->py_object)
    {
        PY_CALL_METHOD_DISCARD(ref->py_object, "on_init", "O", server_python->map_api);
        server_python_check_err();
    }
}

enum client_object_state_t server_python_get_object_default_state(struct server_python_t* server_python, struct server_object_reference_t* ref)
{
    if (ref->py_object)
    {
        PyObject* res = PyObject_CallMethod(ref->py_object, "get_object_state", "");
        
        if (server_python_check_err())
        {
            return OBJECT_STATE_NOTHING;
        }
        else
        {
            long v = PyLong_AsLong(res);
            Py_DecRef(res);
            return v;
        }
    }
    else
    {
        return OBJECT_STATE_NOTHING;
    }
}

void server_python_sync_object_py(struct server_python_t* server_python, struct server_object_reference_t* ref)
{
    if (ref->py_object)
    {
        PY_CALL_METHOD_DISCARD(ref->py_object, "on_sync", "O", server_python->map_api);
        server_python_check_err();
    }
}

void server_python_update_object_py(struct server_python_t* server_python, struct server_object_reference_t* ref)
{
    if (ref->py_object)
    {
        PyObject* py_object = ref->py_object;
        Py_IncRef(py_object);

        PyObject* result = PyObject_CallMethod(py_object, "on_update", "O", server_python->map_api);
        if (result)
        {
            Py_DecRef(result);
        }

        Py_DecRef(py_object);
        server_python_check_err();
    }
}

void server_python_yield_object_state(struct server_python_t* server_python, struct server_object_reference_t* ref)
{
    enum client_object_state_t state = server_python_get_object_default_state(
        &server_python->server_state->server_python, ref);

    map_set_object_state(server_python->server_state, &server_python->server_state->map.map,
        ref, state);
}

void server_python_yield_team(struct server_python_t* server_python, struct server_object_reference_t* ref)
{
    ref->object.team_id = server_python_get_team_id_object_py(server_python, ref);
}

void server_python_damage_object_py(struct server_python_t* server_python, struct server_object_reference_t* ref, int value, char* reason)
{
    if (ref->py_object)
    {
        PY_CALL_METHOD_DISCARD(ref->py_object, "damage", "iy", value, reason);
        server_python_check_err();
    }
}

void server_python_object_on_move(struct server_python_t* server_python, struct server_object_reference_t* ref)
{
    if (ref->py_object)
    {
        PY_CALL_METHOD_DISCARD(ref->py_object, "on_moved", "");
        server_python_check_err();
    }
}

void server_python_on_block_collide_object_py(struct server_python_t* server_python, struct server_object_reference_t* ref)
{
    if (ref->py_object)
    {
        PY_CALL_METHOD_DISCARD(ref->py_object, "on_block_collide", "O", server_python->map_api);
        server_python_check_err();
    }
}

void server_python_on_fall_object_py(struct server_python_t* server_python,
    struct server_object_reference_t* ref, int8_t fall_speed)
{
    if (ref->py_object)
    {
        PY_CALL_METHOD_DISCARD(ref->py_object, "on_fall", "Oi", server_python->map_api, fall_speed);
        server_python_check_err();
    }
}

uint8_t server_python_get_team_id_object_py(struct server_python_t* server_python, struct server_object_reference_t* ref)
{
    if (ref->py_object)
    {
        PyObject* res = PyObject_CallMethod(ref->py_object, "get_team_id", "");

        if (server_python_check_err())
        {
            return 0;
        }

        if (PyLong_Check(res))
        {
            int result = (int)PyLong_AsLong(res);
            Py_DecRef(res);
            return result;
        }

        Py_DecRef(res);
    }

    return 0;
}

void server_python_object_contact_py(struct server_python_t* server_python,
    struct server_object_reference_t* ref, struct server_object_reference_t* contact)
{
    if (ref->py_object && contact->py_object)
    {
        PyObject* py_object = ref->py_object;
        PyObject* contact_object = contact->py_object;
        Py_IncRef(py_object);
        Py_IncRef(contact_object);
        PyObject* result = PyObject_CallMethod(py_object, "on_contact", "OO",
            server_python->map_api, contact_object);
        Py_XDECREF(result);
        Py_DecRef(contact_object);
        Py_DecRef(py_object);

        server_python_check_err();
    }
}

PyObject* server_python_object_serialize(struct server_python_t* server_python,
    struct server_object_reference_t* ref)
{
    if (ref->py_object)
    {
        PyObject* result = PyObject_CallMethod(ref->py_object, "serialize", "");
        server_python_check_err();
        return result;
    }
    else
    {
        Py_RETURN_NONE;
    }
}

uint8_t server_python_object_deserialize(struct server_python_t* server_python,
    struct server_object_reference_t* ref, PyObject* data_from)
{
    if (ref->py_object)
    {
        PY_CALL_METHOD_DISCARD(ref->py_object, "deserialize", "O", data_from);

        if (server_python_check_err())
        {
            return 1;
        }

        return 0;
    }
    else
    {
        return 2;
    }
}

PyObject* server_python_serialize(struct server_python_t* server_python, PyObject* o)
{
    if (o)
    {
        PyObject* result = PyObject_CallMethod(o, "serialize", "");

        server_python_check_err();
        return result;
    }
    else
    {
        Py_RETURN_NONE;
    }
}


extern uint8_t server_python_deserialize(struct server_python_t* server_python, PyObject* o, PyObject* data)
{
    if (o)
    {
        PyObject_CallMethod(o, "deserialize", "O", data);

        if (server_python_check_err())
        {
            return 1;
        }

        return 0;
    }
    else
    {
        return 2;
    }
}

void server_python_terminal(struct server_python_t* server_python,
    struct client_state_t* client_state, const char* command)
{
    if (client_state->py)
    {
        PY_CALL_METHOD_DISCARD(client_state->py, "on_terminal", "y", command);
        server_python_check_err();
    }
}

void server_python_action(struct server_python_t* server_python,
    struct client_state_t* client_state, const char* command, const uint8_t* payload, uint8_t payload_len)
{
    if (client_state->py)
    {
        Py_ssize_t l = payload_len;
        PY_CALL_METHOD_DISCARD(client_state->py, "on_action", "yy#", command, payload_len ? payload : NULL, l);
        server_python_check_err();
    }
}

void server_python_map_serialize(struct server_python_t* server_python, const char* path)
{
    PY_CALL_METHOD_DISCARD(server_python->map_api, "serialize", "y", path);
    server_python_check_err();
}

void server_python_map_deserialize(struct server_python_t* server_python, const char* path)
{
    PY_CALL_METHOD_DISCARD(server_python->map_api, "deserialize", "y", path);
    server_python_check_err();
}

void server_python_object_free_object_py(struct server_python_t* server_python, struct server_object_reference_t* ref)
{
    if (ref->py_object)
    {
        Py_DecRef(ref->py_object);
        ref->py_object = NULL;
        python_printf("freed py object for obj %d\n", ref->object.object_id);
    }

    ref->destroyed = 1;
}

void server_python_block_notifications(struct server_python_t* server_python, struct client_state_t* client_state, const char* reason)
{
    if (client_state->py)
    {
        PY_CALL_METHOD_DISCARD(client_state->py, "block_notifications", "y", reason);
        server_python_check_err();
    }
}

void server_python_unblock_notifications(struct server_python_t* server_python, struct client_state_t* client_state, const char* reason)
{
    if (client_state->py)
    {
        PY_CALL_METHOD_DISCARD(client_state->py, "unblock_notifications", "y", reason);
        server_python_check_err();
    }
}

void server_python_update(struct server_python_t* server_python, struct client_state_t* client_state)
{
    if (client_state->py)
    {
        PY_CALL_METHOD_DISCARD(client_state->py, "on_update", "O", server_python->map_api);
        server_python_check_err();
    }
}

void server_python_update_player(struct server_python_t* server_python, struct client_state_t* client_state)
{
    if (client_state->py_postponed_touch)
    {
        PyObject* update_result = PyObject_CallMethod(client_state->py_postponed_touch, "update", "");

        if (server_python_check_err())
        {
            return;
        }

        if (update_result == Py_True)
        {
            server_state_client_touch_progress(server_python->server_state, client_state, 12);
            server_python_player_touch_cancel(server_python, client_state);

            struct server_object_reference_t* ref = server_map_get_object(
                &server_python->server_state->map, server_state_client_active_object(client_state));

            if (ref)
            {
                enum client_object_state_t state = server_python_get_object_default_state(
                        &server_python->server_state->server_python, ref);
                
                map_set_object_state(
                    server_python->server_state,
                    &server_python->server_state->map.map, ref, state);
            }
        }
        else
        {
            uint32_t progress = PyLong_AsLong(update_result);
            if (progress != client_state->py_postponed_touch_progress)
            {
                server_state_client_touch_progress(server_python->server_state, client_state, progress);
                client_state->py_postponed_touch_progress = progress;
            }
        }

        Py_DecRef(update_result);
    }
}

uint8_t server_python_player_touch_cancel(struct server_python_t* server_python,
    struct client_state_t* client_state)
{
    if (client_state->py_postponed_touch == NULL)
        return 0;

    struct server_object_reference_t* ref = server_map_get_object(
        &server_python->server_state->map, server_state_client_active_object(client_state));

    if (ref)
    {
        PY_CALL_METHOD_DISCARD(ref->py_object, "set_active_postponed_touch", "O", Py_None);
        server_python_check_err();
        server_python_yield_object_state(server_python, ref);
    }

    PY_CALL_METHOD_DISCARD(client_state->py_postponed_touch, "dispose", "");

    server_python_check_err();

    Py_DecRef(client_state->py_postponed_touch);
    client_state->py_postponed_touch = NULL;
    return 1;
}

void server_python_player_touch(struct server_python_t* server_python,
    struct client_state_t* client_state, uint16_t x, uint16_t y,
    uint16_t* scheduled_touch)
{
    if (client_state->py_postponed_touch)
    {
        return;
    }

    struct server_object_reference_t* ref = server_map_get_object(
        &server_python->server_state->map, server_state_client_active_object(client_state));

    if (ref == NULL)
    {
        return;
    }

    if (ref->py_object == NULL)
    {
        return;
    }

    PyObject* result = PyObject_CallMethod(ref->py_object, "on_touch", "ii", x, y);

    if (server_python_check_err())
    {
        return;
    }

    if (result != Py_None)
    {
        client_state->py_postponed_touch = result;
        client_state->py_postponed_touch_progress = 9999;
        PyObject* time_to_remove = PyObject_GetAttrString(client_state->py_postponed_touch, "time_to_remove");
        *scheduled_touch = (uint16_t)(long)PyLong_AsLong(time_to_remove);
        Py_XDECREF(time_to_remove);

        PY_CALL_METHOD_DISCARD(ref->py_object, "set_active_postponed_touch", "O", result);
        server_python_check_err();

        if (*scheduled_touch)
        {
            client_printf(client_state, "Scheduled touch for %hu ms\n", *scheduled_touch);
        }
    }
    else
    {
        *scheduled_touch = 0;
        Py_DecRef(result);
    }
}

void server_python_player_aim(struct server_python_t* server_python,
    struct client_state_t* client_state, uint16_t aim)
{
    struct server_object_reference_t* ref = server_map_get_object(
            &server_python->server_state->map, server_state_client_active_object(client_state));

    if (ref == NULL)
    {
        return;
    }

    if (ref->py_object == NULL)
    {
        return;
    }

    client_printf(client_state, "aim %d\n", aim);
    PY_CALL_METHOD_DISCARD(ref->py_object, "on_aim", "i", aim);
    server_python_check_err();
}

void server_python_player_hit(struct server_python_t* server_python,
    struct client_state_t* client_state, uint16_t angle)
{
    struct server_object_reference_t* ref = server_map_get_object(
        &server_python->server_state->map, server_state_client_active_object(client_state));

    if (ref == NULL)
    {
        return;
    }

    if (ref->py_object == NULL)
    {
        return;
    }

    PY_CALL_METHOD_DISCARD(ref->py_object, "on_hit", "i", angle);

    if (server_python_check_err())
    {
        return;
    }
}

void server_python_client_free(struct server_python_t* server_python, struct client_state_t* client_state)
{
    if (client_state->py)
    {
        PY_CALL_METHOD_DISCARD(client_state->py, "on_destroyed", "");

        if (server_python_check_err())
        {
            return;
        }

        Py_DecRef(client_state->py);
        client_state->py = NULL;
    }

    if (client_state->py_postponed_touch)
    {
        Py_DecRef(client_state->py_postponed_touch);
        client_state->py_postponed_touch = NULL;
    }
}

uint8_t server_python_player_query(struct server_python_t* server_python,
    struct client_state_t* client_state, const char* query)
{
    struct server_object_reference_t* ref = server_map_get_object(
        &server_python->server_state->map, server_state_client_active_object(client_state));

    if (ref == NULL)
    {
        return 1;
    }

    if (ref->py_object == NULL)
    {
        return 2;
    }

    PyObject* result = PyObject_CallMethod(ref->py_object, "on_query", "y", query);
    if (server_python_check_err())
    {
        return 3;
    }

    Py_XDECREF(result);

    return 0;
}

uint8_t server_python_player_query_option(struct server_python_t* server_python,
    struct client_state_t* client_state, size_t option, const char* action)
{
    struct server_object_reference_t* ref = server_map_get_object(
        &server_python->server_state->map, server_state_client_active_object(client_state));
    PyObject* result;

    if (ref == NULL)
    {
        if (client_state->py == NULL)
        {
            return 1;
        }

        result = PyObject_CallMethod(client_state->py, "on_query_option", "iy", option, action);

        if (server_python_check_err())
        {
            return 3;
        }

        Py_XDECREF(result);

        return 0;
    }

    if (ref->py_object == NULL)
    {
        return 2;
    }

    result = PyObject_CallMethod(ref->py_object, "on_query_option", "iy", option, action);

    if (server_python_check_err())
    {
        return 3;
    }

    Py_XDECREF(result);

    return 0;
}

void server_python_player_get_stats(struct server_python_t* server_python,
    struct client_state_t* client_state, uint8_t* health, uint8_t* power, uint8_t* temperature,
    uint8_t* hit_auto, uint8_t* hit_delay, uint16_t* credits, char* default_state, char* building_state)
{
    struct server_object_reference_t* ref = server_map_get_object(
            &client_state->state->map, client_state->client_object);

    if (ref == NULL || ref->py_object == NULL)
    {
        *health = 0;
        *power = 0;
        *temperature = 0;
        strcpy(default_state, "");
        strcpy(building_state, "");
        return;
    }

    *health = PyLong_AsLong(PyObject_GetAttrString(ref->py_object, "health"));
    *power = PyLong_AsLong(PyObject_GetAttrString(ref->py_object, "power"));
    *credits = PyLong_AsLong(PyObject_GetAttrString(ref->py_object, "credits"));
    *temperature = PyLong_AsLong(PyObject_GetAttrString(ref->py_object, "temperature"));
    *hit_auto = PyLong_AsLong(PyObject_GetAttrString(ref->py_object, "hit_auto"));
    *hit_delay = PyLong_AsLong(PyObject_GetAttrString(ref->py_object, "hit_delay"));
    strcpy(default_state, PyBytes_AS_STRING(PyObject_GetAttrString(ref->py_object, "default_state")));
    strcpy(building_state, PyBytes_AS_STRING(PyObject_GetAttrString(ref->py_object, "building_state")));
}

uint8_t server_python_map_serialize_block(struct server_map_chunk_t* chunk, uint8_t x, uint8_t y,
    ProtoObject** serialize_to)
{
    struct block_metadata_t* metadata = &chunk->block_metadata[x + y * MAP_CHUNK_SIZE];

    PyObject* result = PyObject_CallMethod(metadata->py_object, "serialize", "");
    if (result == NULL)
    {
        PyErr_Print();
        PyErr_Clear();
        return 1;
    }

    if (result == Py_False)
    {
        *serialize_to = NULL;
        Py_DecRef(result);
        return 0;
    }

    if (!PyTuple_Check(result))
    {
        Py_DecRef(result);
        return 2;
    }

    PyObject* identity = PyTuple_GetItem(result, 0);
    PyObject* attrs = PyTuple_GetItem(result, 1);

    if (!PyBytes_Check(identity))
    {
        Py_DecRef(result);
        return 3;
    }

    if (!PyDict_Check(attrs))
    {
        Py_DecRef(result);
        return 4;
    }

    const char* identity_str = PyBytes_AsString(identity);

    declare_str_property_on_stack(_identity, OBJ_PROPERTY_ID, identity_str, NULL);
    *serialize_to = py_dict_to_proto_object(attrs, &_identity);
    Py_DecRef(result);

    return 0;
}

uint8_t server_python_map_set_py(struct server_state_t* server_state, struct server_map_t* map,
    uint16_t x, uint16_t y, PyObject* py, uint8_t notify)
{
    struct block_metadata_t* block_object = server_map_get_block_metadata(&map->map, x, y);

    if (block_object == NULL)
    {
        return 1;
    }

    PyObject* old_object = block_object->py_object;

    server_state_cancel_schedule_block_method(server_state, x, y);

    block_object->py_object = py;
    if (block_object->py_object)
    {
        Py_IncRef(block_object->py_object);
        PY_CALL_METHOD_DISCARD(block_object->py_object, "set_location", "ii", x, y);
    }

    if (old_object)
    {
        // call release on an old block, after new has been placed
        PY_CALL_METHOD_DISCARD(old_object, "release", "O", notify ? Py_True : Py_False);
        server_python_check_err();
        Py_DecRef(old_object);
    }

    if (notify)
    {
        server_notify_block_update(server_state, map, x, y);
    }

    return 0;
}

PyObject* server_python_allocate_block(struct server_python_t* server_python, const char* identity,
    ProtoObject* proto)
{
    PyObject* args = PyTuple_New(1);

    PyObject* kwargs = PyDict_New();
    PyObject* proto_dict = proto_object_to_py_dict(proto);
    PyDict_SetItemString(kwargs, "proto", proto_dict);
    Py_XDECREF(proto_dict);

    PyTuple_SetItem(args, 0, PyBytes_FromString(identity));

    PyObject* result = PyObject_Call(server_python->block_spawn, args, kwargs);

    Py_DECREF(args);
    Py_DECREF(kwargs);

    if (result == NULL)
    {
        PyErr_Print();
        PyErr_Clear();
    }

    return result;
}

#pragma clang diagnostic pop
