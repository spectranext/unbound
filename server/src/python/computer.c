#include "server_python.h"
#include "server.h"
#include "computer.h"
#include "network.h"

PyObject* server_map_api_computer_new_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    server_pyton_obj* obj = (server_pyton_obj*)callable;
    uint16_t namespace_id = (uint16_t)PyLong_AsLong(PyTuple_GetItem(args, 0));
    PyObject* pyhash = PyTuple_GetItem(args, 1);

    char* hash = NULL;
    if (pyhash != Py_None)
    {
        hash = PyBytes_AsString(pyhash);
    }

    struct computer_t* computer = server_state_computer_new(obj->server_pyton->server_state, namespace_id, hash);
    PyObject* computer_api = PyObject_CallFunction(obj->server_pyton->computer_api, "");
    computer->computer_api = computer_api;
    Py_IncRef(computer->computer_api);

    if (server_python_check_err())
    {
        Py_RETURN_NONE;
    }

    ASSIGN_COMPUTER_CALLBACK(computer_api, "destroy", computer_destroy_Type, computer_obj)
    ASSIGN_COMPUTER_CALLBACK(computer_api, "session_join", computer_session_join_Type, computer_obj)
    ASSIGN_COMPUTER_CALLBACK(computer_api, "session_leave", computer_session_leave_Type, computer_obj)
    ASSIGN_COMPUTER_CALLBACK(computer_api, "is_powered_on", computer_is_powered_on_Type, computer_obj)
    ASSIGN_COMPUTER_CALLBACK(computer_api, "is_first_session", computer_is_first_session_Type, computer_obj)
    ASSIGN_COMPUTER_CALLBACK(computer_api, "set_power", computer_set_power_Type, computer_obj)
    ASSIGN_COMPUTER_CALLBACK(computer_api, "get_memory", computer_get_memory_Type, computer_obj)
    ASSIGN_COMPUTER_CALLBACK(computer_api, "set_memory", computer_set_memory_Type, computer_obj)
    ASSIGN_COMPUTER_CALLBACK(computer_api, "get_ula", computer_get_ula_Type, computer_obj)
    ASSIGN_COMPUTER_CALLBACK(computer_api, "set_key", computer_set_key_Type, computer_obj)
    ASSIGN_COMPUTER_CALLBACK(computer_api, "get_hostname", computer_get_hostname_Type, computer_obj)
    ASSIGN_COMPUTER_CALLBACK(computer_api, "get_hash", computer_get_hash_Type, computer_obj)
    ASSIGN_COMPUTER_CALLBACK(computer_api, "set_hostname", computer_set_hostname_Type, computer_obj)
    ASSIGN_COMPUTER_CALLBACK(computer_api, "post_message", computer_post_message_Type, computer_obj)
    ASSIGN_COMPUTER_CALLBACK(computer_api, "bind_port_write", computer_bind_port_write_Type, computer_obj)
    ASSIGN_COMPUTER_CALLBACK(computer_api, "bind_port_read", computer_bind_port_read_Type, computer_obj)
    ASSIGN_COMPUTER_CALLBACK(computer_api, "bind_memory_write", computer_bind_memory_write_Type, computer_obj)
    ASSIGN_COMPUTER_CALLBACK(computer_api, "bind_memory_read", computer_bind_memory_read_Type, computer_obj)
    ASSIGN_COMPUTER_CALLBACK(computer_api, "mount_path", computer_mount_path_Type, computer_obj)
    ASSIGN_COMPUTER_CALLBACK(computer_api, "serialize", computer_serialize_Type, computer_obj)
    ASSIGN_COMPUTER_CALLBACK(computer_api, "deserialize", computer_deserialize_Type, computer_obj)
    ASSIGN_COMPUTER_CALLBACK(computer_api, "reboot", computer_reboot_Type, computer_obj)
    ASSIGN_COMPUTER_CALLBACK(computer_api, "nmi", computer_nmi_Type, computer_obj)
    ASSIGN_COMPUTER_CALLBACK(computer_api, "load_snapshot", computer_load_snapshot_Type, computer_obj)

    Py_IncRef(computer_api);
    return computer_api;
}

PyObject* server_map_api_computer_find_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    server_pyton_obj* obj = (server_pyton_obj*)callable;
    char* hash = PyBytes_AsString(PyTuple_GetItem(args, 0));

    struct computer_t* computer = server_state_computer_find_hash(obj->server_pyton->server_state, hash);

    if (computer == NULL)
    {
        Py_RETURN_NONE;
    }

    Py_IncRef(computer->computer_api);
    return computer->computer_api;
}

PyObject* computer_destroy_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    computer_obj* obj = (computer_obj*)callable;
    computer_destroy(obj->computer);
    Py_RETURN_NONE;
}


PyObject* computer_session_join_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    computer_obj* obj = (computer_obj*)callable;
    uint16_t client_id = (uint16_t)PyLong_AsLong(PyTuple_GetItem(args, 0));

    struct client_state_t* client = client_state_find_id(obj->computer->server_state, client_id);
    if (client == NULL)
    {
        Py_RETURN_FALSE;
    }

    if (computer_session_join(obj->computer, client))
    {
        Py_RETURN_TRUE;
    }

    Py_RETURN_FALSE;
}

PyObject* computer_session_leave_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    computer_obj* obj = (computer_obj*)callable;
    computer_session_leave(obj->computer);
    Py_RETURN_NONE;
}

PyObject* computer_is_powered_on_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    computer_obj* obj = (computer_obj*)callable;
    if (obj->computer->running)
    {
        Py_RETURN_TRUE;
    }
    else
    {
        Py_RETURN_FALSE;
    }
}

PyObject* computer_is_first_session_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    computer_obj* obj = (computer_obj*)callable;
    if (obj->computer->first_session)
    {
        Py_RETURN_TRUE;
    }
    else
    {
        Py_RETURN_FALSE;
    }
}

PyObject* computer_set_power_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    computer_obj* obj = (computer_obj*)callable;
    PyObject* v = PyTuple_GetItem(args, 0);
    if (v == Py_True)
    {
        computer_start(obj->computer);
    }
    else
    {
        computer_stop(obj->computer);
    }
    Py_RETURN_NONE;
}

PyObject* computer_get_memory_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    computer_obj* obj = (computer_obj*)callable;

    uint32_t offset = (uint32_t)PyLong_AsLong(PyTuple_GetItem(args, 0));
    uint16_t size = (uint16_t)PyLong_AsLong(PyTuple_GetItem(args, 1));

    char* mem = malloc(size);
    computer_read_memory(obj->computer, offset, (uint8_t*)mem, size);
    PyObject* bytes = PyBytes_FromStringAndSize(mem, size);
    free(mem);
    Py_IncRef(bytes);
    return bytes;
}

PyObject* computer_set_memory_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    computer_obj* obj = (computer_obj*)callable;

    uint32_t offset = (uint32_t)PyLong_AsLong(PyTuple_GetItem(args, 0));
    char* mem = NULL;
    Py_ssize_t len;
    PyBytes_AsStringAndSize(PyTuple_GetItem(args, 1), &mem, &len);

    if (mem)
    {
        computer_write_memory(obj->computer, offset, (uint8_t*)mem, len);
    }

    Py_RETURN_NONE;
}

PyObject* computer_get_ula_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    computer_obj* obj = (computer_obj*)callable;
    PyObject* res = PyLong_FromLong(computer_get_ula_byte(obj->computer));
    Py_IncRef(res);
    return res;
}

PyObject* computer_set_key_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    computer_obj* obj = (computer_obj*)callable;
    uint16_t row = (uint16_t)PyLong_AsLong(PyTuple_GetItem(args, 0));
    uint16_t key = (uint16_t)PyLong_AsLong(PyTuple_GetItem(args, 1));
    computer_session_key_action(obj->computer, row, key);
    Py_RETURN_NONE;
}

PyObject* computer_get_hostname_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    computer_obj* obj = (computer_obj*)callable;
    PyObject* res = PyBytes_FromString(obj->computer->device.hostname);
    Py_IncRef(res);
    return res;
}

PyObject* computer_get_hash_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    computer_obj* obj = (computer_obj*)callable;
    PyObject* res = PyBytes_FromString(obj->computer->hash);
    Py_IncRef(res);
    return res;
}

PyObject* computer_set_hostname_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    computer_obj* obj = (computer_obj*)callable;
    char* new_hostname = PyBytes_AsString(PyTuple_GetItem(args, 0));
    server_state_computer_set_hostname(obj->computer, new_hostname);
    Py_RETURN_NONE;
}

PyObject* computer_serialize_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    computer_obj* obj = (computer_obj*)callable;
    ssize_t sz;
    uint8_t* data = server_state_computer_serialize(obj->computer, &sz);
    Py_ssize_t psz = sz;
    PyObject* res = PyBytes_FromStringAndSize((char*)data, psz);
    Py_IncRef(res);
    free(data);
    return res;
}

PyObject* computer_deserialize_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    computer_obj* obj = (computer_obj*)callable;
    char* s; Py_ssize_t len;
    PyBytes_AsStringAndSize(PyTuple_GetItem(args, 0), &s, &len);
    if (server_python_check_err())
    {
        Py_RETURN_NONE;
    }
    server_state_computer_deserialize(obj->computer, (uint8_t*)s, len);
    Py_RETURN_NONE;
}

PyObject* computer_post_message_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    computer_obj* obj = (computer_obj*)callable;
    char* msg = PyBytes_AsString(PyTuple_GetItem(args, 0));
    bindings_add_message(obj->computer->device.bindings, &obj->computer->device,
                         NETWORK_MESSAGES_PORT, strlen(msg), (const uint8_t*)msg);
    Py_RETURN_NONE;
}

struct py_bind_port_object
{
    struct computer_t* computer;
    PyObject* callback;
};

static const char* py_port_do_write(struct server_main_thread_runnable_args* args)
{
    struct py_bind_port_object* w = args->port_write.user;
    PyObject_CallFunction(w->callback, "i", args->port_write.value);
    return NULL;
}

static void py_port_write_cb(void* user, uint8_t value)
{
    struct py_bind_port_object* w = user;
    struct server_main_thread_runnable_args args;
    args.port_write.user = user;
    args.port_write.value = value;
    server_state_post_runnable(w->computer->server_state, py_port_do_write, args);
}

static const char* py_port_do_read(struct server_main_thread_runnable_args* args)
{
    struct py_bind_port_object* w = args->port_read.user;
    PyObject* res = PyObject_CallFunction(w->callback, "");
    if (res == NULL)
    {
        if (args->port_read.result_ptr)
            *args->port_read.result_ptr = 0;
        return NULL;
    }

    uint8_t result = (uint8_t)PyLong_AsLong(res);
    Py_DecRef(res);

    if (server_python_check_err())
        result = 0;

    args->port_read.result = result;
    if (args->port_read.result_ptr)
        *args->port_read.result_ptr = result;
    return NULL;
}

static uint8_t py_port_read_cb(void* user)
{
    struct py_bind_port_object* w = user;
    uint8_t result = 0;
    struct server_main_thread_runnable_args args = {0};
    args.port_read.user = user;
    args.port_read.result_ptr = &result;
    server_state_post_runnable_wait(w->computer->server_state, py_port_do_read, args, &w->computer->post_wait);
    return result;
}

static const char* py_port_write_released(struct server_main_thread_runnable_args* args)
{
    struct py_bind_port_object* w = args->port_write_bind.user;
    Py_DecRef(w->callback);
    free(w);
    return NULL;
}

static const char* py_port_read_released(struct server_main_thread_runnable_args* args)
{
    struct py_bind_port_object* w = args->port_read_bind.user;
    Py_DecRef(w->callback);
    free(w);
    return NULL;
}

PyObject* computer_bind_port_write_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    computer_obj* obj = (computer_obj*)callable;
    uint16_t address = PyLong_AsLong(PyTuple_GetItem(args, 0));
    PyObject* cb = PyTuple_GetItem(args, 1);

    struct py_bind_port_object* w = calloc(1, sizeof(struct py_bind_port_object));
    w->computer = obj->computer;
    w->callback = cb;
    Py_IncRef(w->callback);
    computer_bind_port_write(obj->computer, address, py_port_write_cb, py_port_write_released, w);
    Py_RETURN_NONE;
}

PyObject* computer_bind_port_read_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    computer_obj* obj = (computer_obj*)callable;
    uint16_t address = PyLong_AsLong(PyTuple_GetItem(args, 0));
    PyObject* cb = PyTuple_GetItem(args, 1);

    struct py_bind_port_object* w = calloc(1, sizeof(struct py_bind_port_object));
    w->computer = obj->computer;
    w->callback = cb;
    Py_IncRef(w->callback);
    computer_bind_port_read(obj->computer, address, py_port_read_cb, py_port_read_released, w);
    Py_RETURN_NONE;
}

static const char* py_memory_do_write(struct server_main_thread_runnable_args* args)
{
    struct py_bind_port_object* w = args->memory_write.user;
    PyObject_CallFunction(w->callback, "ii", args->memory_write.offset, args->memory_write.value);
    return NULL;
}

static void py_memory_write_cb(void* user, uint16_t offset, uint8_t value)
{
    struct py_bind_port_object* w = user;
    struct server_main_thread_runnable_args args;
    args.memory_write.user = user;
    args.memory_write.offset = offset;
    args.memory_write.value = value;
    server_state_post_runnable(w->computer->server_state, py_memory_do_write, args);
}

static const char* py_memory_do_read(struct server_main_thread_runnable_args* args)
{
    struct py_bind_port_object* w = args->memory_read.user;
    PyObject* res = PyObject_CallFunction(w->callback, "i", args->memory_read.offset);
    args->memory_read.result = PyLong_AsLong(res);
    return NULL;
}

static uint8_t py_memory_read_cb(void* user, uint16_t offset)
{
    struct py_bind_port_object* w = user;
    struct server_main_thread_runnable_args args;
    args.memory_read.user = user;
    args.memory_read.offset = offset;
    server_state_post_runnable_wait(w->computer->server_state, py_memory_do_read, args, &w->computer->post_wait);
    return args.memory_read.result;
}

static const char* py_memory_write_released(struct server_main_thread_runnable_args* args)
{
    struct py_bind_port_object* w = args->memory_write_bind.user;
    Py_DecRef(w->callback);
    free(w);
    return NULL;
}

static const char* py_memory_read_released(struct server_main_thread_runnable_args* args)
{
    struct py_bind_port_object* w = args->memory_read_bind.user;
    Py_DecRef(w->callback);
    free(w);
    return NULL;
}

PyObject* computer_bind_memory_write_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    computer_obj* obj = (computer_obj*)callable;
    uint16_t address = PyLong_AsLong(PyTuple_GetItem(args, 0));
    uint16_t size = PyLong_AsLong(PyTuple_GetItem(args, 1));
    PyObject* cb = PyTuple_GetItem(args, 2);

    struct py_bind_port_object* w = calloc(1, sizeof(struct py_bind_port_object));
    w->computer = obj->computer;
    w->callback = cb;
    Py_IncRef(w->callback);
    computer_bind_memory_write(obj->computer, address, size, py_memory_write_cb, py_memory_write_released, w);
    Py_RETURN_NONE;
}

PyObject* computer_bind_memory_read_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    computer_obj* obj = (computer_obj*)callable;
    uint16_t address = PyLong_AsLong(PyTuple_GetItem(args, 0));
    uint16_t size = PyLong_AsLong(PyTuple_GetItem(args, 1));
    PyObject* cb = PyTuple_GetItem(args, 2);

    struct py_bind_port_object* w = calloc(1, sizeof(struct py_bind_port_object));
    w->computer = obj->computer;
    w->callback = cb;
    Py_IncRef(w->callback);
    computer_bind_memory_read(obj->computer, address, size, py_memory_read_cb, py_memory_read_released, w);
    Py_RETURN_NONE;
}

struct py_xfs_path_object
{
    struct computer_t* computer;
    PyObject* factory;
};

static const char* py_xfs_do_open(struct server_main_thread_runnable_args* args)
{
    struct py_xfs_path_object* w = args->xfs_open.user;
    PyObject* opened = NULL;

    if (PyCallable_Check(w->factory))
    {
        opened = PyObject_CallFunction(w->factory, "yi", args->xfs_open.path, args->xfs_open.flags);
    }
    else
    {
        opened = w->factory;
        Py_IncRef(opened);
    }

    if (server_python_check_err() || !opened)
    {
        *args->xfs_open.result = XFS_ERR_IO;
        return NULL;
    }

    if (opened == Py_None)
    {
        Py_DecRef(opened);
        *args->xfs_open.result = XFS_ERR_IO;
        return NULL;
    }

    *args->xfs_open.handle = opened;
    *args->xfs_open.result = XFS_ERR_OK;
    return NULL;
}

static int16_t py_xfs_open_cb(void* user, const char* path, int flags, uint8_t is_dir, void** out_handle)
{
    struct py_xfs_path_object* w = user;
    struct server_main_thread_runnable_args args;
    int16_t result = XFS_ERR_OK;

    args.xfs_open.user = user;
    args.xfs_open.path = path;
    args.xfs_open.flags = flags;
    args.xfs_open.is_dir = is_dir;
    args.xfs_open.handle = out_handle;
    args.xfs_open.result = &result;
    *out_handle = NULL;

    server_state_post_runnable_wait(w->computer->server_state, py_xfs_do_open, args, &w->computer->post_wait);
    return result;
}

static const char* py_xfs_do_read(struct server_main_thread_runnable_args* args)
{
    PyObject* opened = args->xfs_read.handle;
    PyObject* res = NULL;
    char* data = NULL;
    Py_ssize_t len = 0;

    if (!opened || !PyObject_HasAttrString(opened, "read"))
    {
        *args->xfs_read.result = XFS_ERR_BADF;
        return NULL;
    }

    res = PyObject_CallMethod(opened, "read", "i", args->xfs_read.size);
    if (server_python_check_err() || !res)
    {
        *args->xfs_read.result = XFS_ERR_IO;
        return NULL;
    }

    if (res == Py_None)
    {
        Py_DecRef(res);
        *args->xfs_read.result = 0;
        return NULL;
    }

    if (PyUnicode_Check(res))
    {
        PyObject* bytes = PyUnicode_AsUTF8String(res);
        Py_DecRef(res);
        res = bytes;
    }

    if (!res || PyBytes_AsStringAndSize(res, &data, &len) != 0)
    {
        server_python_check_err();
        Py_XDECREF(res);
        *args->xfs_read.result = XFS_ERR_IO;
        return NULL;
    }

    if (len > args->xfs_read.size)
        len = args->xfs_read.size;
    memcpy(args->xfs_read.buffer, data, len);
    Py_DecRef(res);
    *args->xfs_read.result = (int16_t)len;
    return NULL;
}

static int16_t py_xfs_read_cb(void* user, void* handle, uint8_t* buffer, uint16_t size)
{
    struct py_xfs_path_object* w = user;
    struct server_main_thread_runnable_args args;
    int16_t result = XFS_ERR_OK;

    args.xfs_read.user = user;
    args.xfs_read.handle = handle;
    args.xfs_read.buffer = buffer;
    args.xfs_read.size = size;
    args.xfs_read.result = &result;

    server_state_post_runnable_wait(w->computer->server_state, py_xfs_do_read, args, &w->computer->post_wait);
    return result;
}

static const char* py_xfs_do_write(struct server_main_thread_runnable_args* args)
{
    PyObject* opened = args->xfs_write.handle;
    PyObject* data = NULL;
    PyObject* res = NULL;

    if (!opened || !PyObject_HasAttrString(opened, "write"))
    {
        *args->xfs_write.result = XFS_ERR_BADF;
        return NULL;
    }

    data = PyBytes_FromStringAndSize((const char*)args->xfs_write.buffer, args->xfs_write.size);
    res = PyObject_CallMethod(opened, "write", "O", data);
    Py_XDECREF(data);
    if (server_python_check_err() || !res)
    {
        *args->xfs_write.result = XFS_ERR_IO;
        return NULL;
    }

    if (res == Py_None)
    {
        *args->xfs_write.result = args->xfs_write.size;
    }
    else
    {
        *args->xfs_write.result = (int16_t)PyLong_AsLong(res);
    }
    Py_DecRef(res);
    return NULL;
}

static int16_t py_xfs_write_cb(void* user, void* handle, const uint8_t* buffer, uint16_t size)
{
    struct py_xfs_path_object* w = user;
    struct server_main_thread_runnable_args args;
    int16_t result = XFS_ERR_OK;

    args.xfs_write.user = user;
    args.xfs_write.handle = handle;
    args.xfs_write.buffer = buffer;
    args.xfs_write.size = size;
    args.xfs_write.result = &result;

    server_state_post_runnable_wait(w->computer->server_state, py_xfs_do_write, args, &w->computer->post_wait);
    return result;
}

static const char* py_xfs_do_seek(struct server_main_thread_runnable_args* args)
{
    PyObject* opened = args->xfs_seek.handle;
    PyObject* res = NULL;

    if (!opened || !PyObject_HasAttrString(opened, "seek"))
    {
        *args->xfs_seek.result = XFS_ERR_INVAL;
        return NULL;
    }

    res = PyObject_CallMethod(opened, "seek", "iI", args->xfs_seek.mode, args->xfs_seek.offset);
    if (server_python_check_err() || !res)
    {
        *args->xfs_seek.result = XFS_ERR_IO;
        return NULL;
    }

    *args->xfs_seek.result = (int32_t)PyLong_AsLong(res);
    Py_DecRef(res);
    return NULL;
}

static int32_t py_xfs_seek_cb(void* user, void* handle, uint8_t mode, uint32_t offset)
{
    struct py_xfs_path_object* w = user;
    struct server_main_thread_runnable_args args;
    int32_t result = XFS_ERR_OK;

    args.xfs_seek.user = user;
    args.xfs_seek.handle = handle;
    args.xfs_seek.mode = mode;
    args.xfs_seek.offset = offset;
    args.xfs_seek.result = &result;

    server_state_post_runnable_wait(w->computer->server_state, py_xfs_do_seek, args, &w->computer->post_wait);
    return result;
}

static const char* py_xfs_do_close(struct server_main_thread_runnable_args* args)
{
    PyObject* opened = args->xfs_close.handle;
    PyObject* res = NULL;

    if (opened && PyObject_HasAttrString(opened, "close"))
    {
        res = PyObject_CallMethod(opened, "close", "");
        if (server_python_check_err())
        {
            Py_XDECREF(res);
            *args->xfs_close.result = XFS_ERR_IO;
            return NULL;
        }
        Py_XDECREF(res);
    }

    Py_XDECREF((PyObject*)args->xfs_close.handle);
    *args->xfs_close.result = XFS_ERR_OK;
    return NULL;
}

static int16_t py_xfs_close_cb(void* user, void* handle)
{
    struct py_xfs_path_object* w = user;
    struct server_main_thread_runnable_args args;
    int16_t result = XFS_ERR_OK;

    args.xfs_close.user = user;
    args.xfs_close.handle = handle;
    args.xfs_close.result = &result;

    if (PyGILState_Check())
        py_xfs_do_close(&args);
    else
        server_state_post_runnable_wait(w->computer->server_state, py_xfs_do_close, args, &w->computer->post_wait);
    return result;
}

static int py_xfs_name_from_object(PyObject* obj, char* name, size_t name_size, uint8_t* is_dir, uint32_t* size)
{
    char* raw = NULL;
    Py_ssize_t len = 0;
    PyObject* name_obj = obj;
    PyObject* bytes = NULL;

    *is_dir = 0;
    *size = 0;

    if (PyTuple_Check(obj) || PyList_Check(obj))
    {
        Py_ssize_t seq_size = PySequence_Size(obj);
        if (seq_size < 1)
            return 0;
        name_obj = PySequence_GetItem(obj, 0);
        if (seq_size > 1)
        {
            PyObject* type_obj = PySequence_GetItem(obj, 1);
            *is_dir = PyObject_IsTrue(type_obj) ? 1 : 0;
            Py_XDECREF(type_obj);
        }
        if (seq_size > 2)
        {
            PyObject* size_obj = PySequence_GetItem(obj, 2);
            *size = (uint32_t)PyLong_AsUnsignedLong(size_obj);
            Py_XDECREF(size_obj);
            if (server_python_check_err())
            {
                Py_XDECREF(name_obj);
                return 0;
            }
        }
    }
    else
    {
        Py_IncRef(name_obj);
    }

    if (PyUnicode_Check(name_obj))
    {
        bytes = PyUnicode_AsUTF8String(name_obj);
        Py_DecRef(name_obj);
        name_obj = bytes;
    }

    if (!name_obj || PyBytes_AsStringAndSize(name_obj, &raw, &len) != 0)
    {
        server_python_check_err();
        Py_XDECREF(name_obj);
        return 0;
    }

    if (len <= 0 || (size_t)len >= name_size)
    {
        Py_DecRef(name_obj);
        return 0;
    }

    memcpy(name, raw, len);
    name[len] = '\0';
    Py_DecRef(name_obj);
    return 1;
}

static const char* py_xfs_do_readdir(struct server_main_thread_runnable_args* args)
{
    PyObject* opened = args->xfs_readdir.handle;
    PyObject* res = NULL;
    uint8_t is_dir = 0;
    uint32_t size = 0;

    if (!opened || !PyObject_HasAttrString(opened, "readdir"))
    {
        *args->xfs_readdir.result = 0;
        return NULL;
    }

    res = PyObject_CallMethod(opened, "readdir", "");
    if (server_python_check_err() || !res)
    {
        *args->xfs_readdir.result = XFS_ERR_IO;
        return NULL;
    }

    if (res == Py_None)
    {
        Py_DecRef(res);
        *args->xfs_readdir.result = 0;
        return NULL;
    }

    memset(args->xfs_readdir.info, 0, sizeof(*args->xfs_readdir.info));
    if (!py_xfs_name_from_object(res, args->xfs_readdir.info->name, sizeof(args->xfs_readdir.info->name), &is_dir, &size))
    {
        Py_DecRef(res);
        *args->xfs_readdir.result = XFS_ERR_IO;
        return NULL;
    }

    args->xfs_readdir.info->type = is_dir ? XFS_TYPE_DIR : XFS_TYPE_REG;
    args->xfs_readdir.info->size = size;
    Py_DecRef(res);
    *args->xfs_readdir.result = 1;
    return NULL;
}

static int16_t py_xfs_readdir_cb(void* user, void* handle, struct xfs_stat_info* info)
{
    struct py_xfs_path_object* w = user;
    struct server_main_thread_runnable_args args;
    int16_t result = XFS_ERR_OK;

    args.xfs_readdir.user = user;
    args.xfs_readdir.handle = handle;
    args.xfs_readdir.info = info;
    args.xfs_readdir.result = &result;

    server_state_post_runnable_wait(w->computer->server_state, py_xfs_do_readdir, args, &w->computer->post_wait);
    return result;
}

static const char* py_xfs_path_released(struct server_main_thread_runnable_args* args)
{
    struct py_xfs_path_object* w = args->xfs_path_bind.user;
    Py_DecRef(w->factory);
    free(w);
    return NULL;
}

PyObject* computer_mount_path_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    computer_obj* obj = (computer_obj*)callable;
    PyObject* path_obj = PyTuple_GetItem(args, 0);
    PyObject* factory = PyTuple_GetItem(args, 1);
    const char* path = NULL;

    if (PyBytes_Check(path_obj))
        path = PyBytes_AsString(path_obj);
    else if (PyUnicode_Check(path_obj))
        path = PyUnicode_AsUTF8(path_obj);

    if (!path)
    {
        PyErr_SetString(PyExc_TypeError, "mount_path path must be bytes or str");
        return NULL;
    }

    struct py_xfs_path_object* w = calloc(1, sizeof(struct py_xfs_path_object));
    if (!w)
    {
        PyErr_NoMemory();
        return NULL;
    }

    w->computer = obj->computer;
    w->factory = factory;
    Py_IncRef(w->factory);

    if (!computer_mount_path(obj->computer, path, PyObject_HasAttrString(factory, "readdir") ? 1 : 0,
                             py_xfs_open_cb, py_xfs_read_cb, py_xfs_write_cb, py_xfs_close_cb, py_xfs_seek_cb,
                             py_xfs_readdir_cb, py_xfs_path_released, w))
    {
        Py_DecRef(w->factory);
        free(w);
        Py_RETURN_FALSE;
    }

    Py_RETURN_TRUE;
}

PyObject* computer_reboot_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    computer_obj* obj = (computer_obj*)callable;
    computer_reboot(obj->computer);
    Py_RETURN_NONE;
}

PyObject* computer_nmi_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    computer_obj* obj = (computer_obj*)callable;
    computer_nmi(obj->computer);
    Py_RETURN_NONE;
}

PyObject* computer_load_snapshot_cb(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    computer_obj* obj = (computer_obj*)callable;
    char* s; Py_ssize_t len;
    PyBytes_AsStringAndSize(PyTuple_GetItem(args, 0), &s, &len);
    if (server_python_check_err())
    {
        Py_RETURN_FALSE;
    }

    if (computer_snapshot_load(obj->computer, (uint8_t*)s, len) == 0)
    {
        Py_RETURN_TRUE;
    }
    else
    {
        Py_RETURN_FALSE;
    }
}

void server_python_computer_notify_event(struct computer_t* computer, const char* event)
{
    if (event == NULL)
    {
        server_printf("computer %s is being destroyed\n", computer->device.hostname);
        PyObject_CallMethod(computer->computer_api, "notify_event", "O", Py_None);
    }
    else
    {
        server_printf("computer %s event: %s\n", computer->device.hostname, event);
        PyObject_CallMethod(computer->computer_api, "notify_event", "y", event);
    }
    server_python_check_err();
}

void server_python_computer_notify_log_message(struct computer_t* computer, const char* message)
{
    if (message == NULL)
        return;

    PyObject_CallMethod(computer->computer_api, "notify_log_message", "y", message);
    server_python_check_err();
}
