#include <sys/socket.h>
#include "server_python.h"
#include "server.h"
#include "network.h"

PyObject* device_destroy_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    server_network_device_obj* obj = (server_network_device_obj*)callable;
    Py_DecRef(obj->device->handler);
    server_state_device_free(obj->device->server_state, obj->device);
    Py_RETURN_NONE;
}

PyObject* device_listen_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    server_network_device_obj* obj = (server_network_device_obj*)callable;
    PyObject* handler = PyTuple_GetItem(args, 0);

    if (obj->device->handler)
    {
        Py_DecRef(obj->device->handler);
    }

    obj->device->handler = handler;
    Py_IncRef(obj->device->handler);

    server_state_device_listen(obj->device);
    Py_RETURN_NONE;
}

PyObject* device_listen_close_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    server_network_device_obj* obj = (server_network_device_obj*)callable;
    server_state_device_listen_close(obj->device);
    Py_RETURN_NONE;
}

PyObject* device_get_hostname_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    server_network_device_obj* obj = (server_network_device_obj*)callable;
    PyObject* res = PyBytes_FromString(obj->device->device.hostname);
    Py_IncRef(res);
    return res;
}

PyObject* device_set_hostname_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    server_network_device_obj* obj = (server_network_device_obj*)callable;
    char* new_hostname = PyBytes_AsString(PyTuple_GetItem(args, 0));
    network_device_assign_hostname(&obj->device->device, new_hostname);
    Py_RETURN_NONE;
}

PyObject* device_post_message_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    server_network_device_obj* obj = (server_network_device_obj*)callable;
    char* msg = PyBytes_AsString(PyTuple_GetItem(args, 0));
    // post a message to itself
    bindings_add_message(obj->device->device.bindings, &obj->device->device,
                         NETWORK_MESSAGES_PORT, strlen(msg), (const uint8_t*)msg);
    Py_RETURN_NONE;
}

PyObject* device_get_device_id_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    server_network_device_obj* obj = (server_network_device_obj*)callable;
    PyObject* res = PyLong_FromLong(obj->device->device.device_id);
    Py_IncRef(res);
    return res;
}

PyObject* device_connect_to_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    server_network_device_obj* obj = (server_network_device_obj*)callable;
    char* hostname = PyBytes_AsString(PyTuple_GetItem(args, 0));
    uint16_t port = (uint16_t)PyLong_AsLong(PyTuple_GetItem(args, 1));
    PyObject* handler = PyTuple_GetItem(args, 2);

    struct network_device_t* to = network_device_find(&obj->device->server_state->network_bindings, hostname);
    if (to == NULL)
    {
        Py_RETURN_NONE;
    }

    struct server_network_device_session_t* session = network_device_connect_to(obj->device, to, port, handler);
    if (session == NULL)
    {
        Py_RETURN_NONE;
    }

    Py_IncRef(session->session);
    return session->session;
}

PyObject* device_session_write_data_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    server_device_session_obj* obj = (server_device_session_obj*)callable;
    char* s; Py_ssize_t len;
    PyBytes_AsStringAndSize(PyTuple_GetItem(args, 0), &s, &len);
    send(obj->session->socket_fd, s, len, 0);
    Py_RETURN_NONE;
}

PyObject* device_session_close_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    server_device_session_obj* obj = (server_device_session_obj*)callable;
    shutdown(obj->session->socket_fd, SHUT_RDWR);
    Py_RETURN_NONE;
}

PyObject* server_map_api_device_new_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    server_pyton_obj* obj = (server_pyton_obj*)callable;
    uint16_t namespace_id = (uint16_t)PyLong_AsLong(PyTuple_GetItem(args, 0));
    char* prefix = PyBytes_AsString(PyTuple_GetItem(args, 1));

    struct server_network_device_t* device = server_state_device_new(obj->server_pyton->server_state, namespace_id, prefix);
    if (device == NULL)
    {
        Py_RETURN_NONE;
    }

    device->server_state = obj->server_pyton->server_state;
    device->handler = NULL;

    device->py = PyObject_CallFunction(obj->server_pyton->device_api, "");
    Py_IncRef(device->py);

    if (server_python_check_err())
    {
        Py_RETURN_NONE;
    }

    ASSIGN_DEVICE_CALLBACK(device->py, "destroy", device_destroy_Type, server_network_device_obj)
    ASSIGN_DEVICE_CALLBACK(device->py, "listen", device_listen_Type, server_network_device_obj)
    ASSIGN_DEVICE_CALLBACK(device->py, "listen_close", device_listen_close_Type, server_network_device_obj)
    ASSIGN_DEVICE_CALLBACK(device->py, "get_hostname", device_get_hostname_Type, server_network_device_obj)
    ASSIGN_DEVICE_CALLBACK(device->py, "set_hostname", device_set_hostname_Type, server_network_device_obj)
    ASSIGN_DEVICE_CALLBACK(device->py, "post_message", device_post_message_Type, server_network_device_obj)
    ASSIGN_DEVICE_CALLBACK(device->py, "get_device_id", device_get_device_id_Type, server_network_device_obj)
    ASSIGN_DEVICE_CALLBACK(device->py, "connect_to", device_connect_to_Type, server_network_device_obj)

    return device->py;
}

extern PyObject* server_python_device_get_session_handler(struct server_network_device_t* device)
{
    if (device->handler == NULL)
        return NULL;

    return PyObject_GetAttrString(device->handler, "on_device_session");
}

PyObject* server_python_device_api_allocate_session(
    struct server_python_t* server_pyton,
    struct server_network_device_session_t* session,
    struct server_network_device_t* device, PyObject* handler)
{
    PyObject* py = PyObject_CallMethod(device->py, "session_new", "");
    if (py == NULL)
    {
        server_python_check_err();
        return NULL;
    }

    Py_IncRef(py);

    ASSIGN_DEVICE_SESSION_CALLBACK(py, "write_data", device_session_write_data_Type, server_device_session_obj)
    ASSIGN_DEVICE_SESSION_CALLBACK(py, "close", device_session_cose_Type, server_device_session_obj)

    Py_IncRef(handler);
    PyObject_CallMethod(py, "start_session", "O", handler);
    Py_DecRef(handler);
    server_python_check_err();

    return py;
}

void server_python_device_session_api_free(PyObject* session)
{
    PyObject_CallMethod(session, "on_closed", "");
    Py_DecRef(session);
}

void server_python_device_session_api_on_data(PyObject* session, const uint8_t* data, size_t len)
{
    PyObject* po = PyBytes_FromStringAndSize((const char*)data, (Py_ssize_t)len);
    Py_IncRef(po);
    PyObject_CallMethod(session, "on_data", "O", po);
    Py_DecRef(po);
}
