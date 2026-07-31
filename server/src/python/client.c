#include "server_python.h"
#include "server.h"
#include "messages.h"
#include "utils.h"
#include <sys/socket.h>
#include <string.h>

#define CLIENT_CHAT_MESSAGE_SIZE 64

static struct client_state_t* client_api_get_state(PyObject* callable)
{
    client_pyton_obj* obj = (client_pyton_obj*)callable;
    if (obj->client_state)
    {
        return obj->client_state;
    }
    if (obj->server_state == NULL)
    {
        return NULL;
    }
    return client_state_find_id(obj->server_state, obj->client_id);
}

#define CLIENT_API_STATE_OR_NONE()                         \
    struct client_state_t* client_state = client_api_get_state(callable); \
    if (client_state == NULL)                              \
    {                                                      \
        Py_RETURN_NONE;                                    \
    }

PyObject* client_api_notify_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    CLIENT_API_STATE_OR_NONE();

    const char* msg = PyBytes_AS_STRING(PyTuple_GetItem(args, 0));
    uint8_t color = (uint8_t)PyLong_AsLong(PyTuple_GetItem(args, 1));

    client_state_notify_message(client_state->state, client_state, msg, color);
    Py_RETURN_NONE;
}

PyObject* client_api_disconnect_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    CLIENT_API_STATE_OR_NONE();
    shutdown(client_state->client_socket, SHUT_RDWR);
    Py_RETURN_NONE;
}

PyObject* client_api_sync_stats_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    CLIENT_API_STATE_OR_NONE();
    server_state_client_sync_stats(client_state, client_state->state);
    Py_RETURN_NONE;
}

PyObject* client_api_send_chat_message_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    CLIENT_API_STATE_OR_NONE();
    PyObject* msg_bytes = PyTuple_GetItem(args, 0);
    const char* msg = PyBytes_AS_STRING(msg_bytes);
    Py_ssize_t msg_size = PyBytes_GET_SIZE(msg_bytes);
    char bounded_msg[CLIENT_CHAT_MESSAGE_SIZE];

    if (msg_size >= CLIENT_CHAT_MESSAGE_SIZE)
    {
        msg_size = CLIENT_CHAT_MESSAGE_SIZE - 1;
    }
    memcpy(bounded_msg, msg, msg_size);
    bounded_msg[msg_size] = '\0';

    declare_str_property_on_stack(_m, 'm', bounded_msg, NULL);
    uint8_t command = MSG_CHAT;
    declare_arg_property_on_stack(id, OBJ_PROPERTY_ID, command, &_m);
    server_printf("chat: queue send to client_id=%d user=%s message=%s\n",
        client_state->client_id, client_state->user_name, bounded_msg);
    client_state_send_proto_one_object(client_state->state, client_state, &id);
    Py_RETURN_NONE;
}

PyObject* client_api_push_module_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    CLIENT_API_STATE_OR_NONE();

    char* module = PyBytes_AsString(PyTuple_GetItem(args, 0));
    server_data_push_module(client_state, module);

    Py_RETURN_NONE;
}

PyObject* client_api_list_modules_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    struct client_state_t* client_state = client_api_get_state(callable);
    if (client_state == NULL)
    {
        return PyTuple_New(0);
    }

    struct server_data_entry_t* e;
    struct server_data_entry_t* tmp;

    int count;
    LL_COUNT(client_state->state->server_data_modules.data_entries, e, count);

    PyObject* ret = PyTuple_New(count);
    int i = 0;

    LL_FOREACH_SAFE(client_state->state->server_data_modules.data_entries, e, tmp)
    {
        PyTuple_SetItem(ret, i++, PyBytes_FromString(e->name));
    }

    return ret;
}

PyObject* client_api_get_module_prop_int_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    char* module_name = PyBytes_AsString(PyTuple_GetItem(args, 0));
    char* key = PyBytes_AsString(PyTuple_GetItem(args, 1));
    long def = PyLong_AsLong(PyTuple_GetItem(args, 2));
    struct client_state_t* client_state = client_api_get_state(callable);
    if (client_state == NULL)
    {
        return Py_BuildValue("i", def);
    }

    struct server_data_entry_t* e = find_data_entry(&client_state->state->server_data_modules, module_name);
    if (e == NULL)
    {
        return Py_BuildValue("i", def);
    }

    long value = get_data_entry_prop_int(e, key, def);
    return Py_BuildValue("i", value);
}

PyObject* client_api_get_module_prop_str_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    CLIENT_API_STATE_OR_NONE();

    char* module_name = PyBytes_AsString(PyTuple_GetItem(args, 0));
    char* key = PyBytes_AsString(PyTuple_GetItem(args, 1));
    struct server_data_entry_t* e = find_data_entry(&client_state->state->server_data_modules, module_name);
    if (e == NULL)
    {
        Py_RETURN_NONE;
    }

    const char* value = get_data_entry_prop_str(e, key);
    if (value == NULL)
    {
        Py_RETURN_NONE;
    }

    return PyBytes_FromString(value);
}

PyObject* client_api_push_screen_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    CLIENT_API_STATE_OR_NONE();

    char* screen_name = PyBytes_AsString(PyTuple_GetItem(args, 0));
    server_push_screen(client_state->state, client_state, screen_name);

    Py_RETURN_NONE;
}

PyObject* client_api_push_memory_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    CLIENT_API_STATE_OR_NONE();

    uint16_t address = PyLong_AsLong(PyTuple_GetItem(args, 0));
    char* data;
    Py_ssize_t sz;
    PyBytes_AsStringAndSize(PyTuple_GetItem(args, 1), &data, &sz);
    server_push_memory(client_state->state, client_state, address, (uint8_t*)data, sz);

    Py_RETURN_NONE;
}

PyObject* client_api_module_action_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    CLIENT_API_STATE_OR_NONE();

    char* module = PyBytes_AsString(PyTuple_GetItem(args, 0));
    PyObject* action = PyTuple_GetItem(args, 1);
    server_data_module_action(client_state, module, action);

    Py_RETURN_NONE;
}

PyObject* client_api_force_watch_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    CLIENT_API_STATE_OR_NONE();

    uint16_t x = PyLong_AsLong(PyTuple_GetItem(args, 0));
    uint16_t y = PyLong_AsLong(PyTuple_GetItem(args, 1));

    if (x > 16)
    {
        x -= 16;
    }
    else
    {
        x = 0;
    }

    if (y > 8)
    {
        y -= 8;
    }
    else
    {
        y = 0;
    }

    uint8_t _x = x / MAP_CHUNK_SIZE;
    uint8_t _y = y / MAP_CHUNK_SIZE;

    server_state_client_set_watch(client_state, client_state->state, _x, _y);

    Py_RETURN_NONE;
}

PyObject* client_api_get_name_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    struct client_state_t* client_state = client_api_get_state(callable);
    PyObject* b = PyBytes_FromString(client_state ? client_state->user_name : "");
    return b;
}

PyObject* client_api_get_user_id_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    struct client_state_t* client_state = client_api_get_state(callable);
    PyObject* b = PyBytes_FromString(client_state ? client_state->user_id : "");
    return b;
}

PyObject* client_api_set_hit_controls_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    CLIENT_API_STATE_OR_NONE();
    uint16_t object_id = PyLong_AsLong(PyTuple_GetItem(args, 0));

    server_state_client_sync_stats(client_state, client_state->state);

    PyObject* b = PyBytes_FromString(client_state->user_id);
    return b;
}

PyObject* client_api_set_object_control_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    CLIENT_API_STATE_OR_NONE();
    uint16_t object_id = PyLong_AsLong(PyTuple_GetItem(args, 0));
    server_state_client_set_active_object(client_state, object_id);
    Py_RETURN_NONE;
}

PyObject* client_api_on_team_set_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    CLIENT_API_STATE_OR_NONE();
    client_state->team_id = PyLong_AsLong(PyTuple_GetItem(args, 0));
    Py_RETURN_NONE;
}

PyObject* client_api_get_client_object_id_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    struct client_state_t* client_state = client_api_get_state(callable);
    PyObject* r = PyLong_FromLong(client_state ? client_state->client_object : 0);
    return r;
}

PyObject* client_api_get_control_object_id_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    struct client_state_t* client_state = client_api_get_state(callable);
    PyObject* r = PyLong_FromLong(client_state ? client_state->control_object : 0);
    return r;
}
