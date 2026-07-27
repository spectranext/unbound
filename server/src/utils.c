#include "utils.h"
#include "server.h"
#include "messages.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <sys/stat.h>
#include <ut/uthash.h>

uint8_t* hex_to_bytes(char* string, uint16_t* out_array_len)
{
    if(string == NULL)
        return NULL;

    uint16_t array_len = strlen(string);
    if((array_len % 2) != 0) // must be even
        return NULL;

    *out_array_len = array_len / 2;

    uint8_t* data = malloc(*out_array_len);
    memset(data, 0, *out_array_len);

    size_t index = 0;
    while (index < array_len) {
        char c = string[index];
        int value = 0;
        if(c >= '0' && c <= '9')
            value = (c - '0');
        else if (c >= 'A' && c <= 'F')
            value = (10 + (c - 'A'));
        else if (c >= 'a' && c <= 'f')
            value = (10 + (c - 'a'));
        else {
            free(data);
            return NULL;
        }

        data[(index/2)] += value << (((index + 1) % 2) * 4);

        index++;
    }

    return data;
}

uint8_t file_exists(const char *filename)
{
    struct stat   buffer;
    return (stat (filename, &buffer) == 0);
}

PyObject* proto_object_to_py_dict(ProtoObject* proto_object)
{
    PyObject* dict = PyDict_New();

    if (proto_object)
    {
        for (ProtoObjectPropertyPtr* prop = proto_object->properties; *prop; prop++)
        {
            PyDict_SetItem(dict,
                PyBytes_FromStringAndSize((char*)&(*prop)->key, 1),
                PyBytes_FromStringAndSize((*prop)->value, (*prop)->value_size));
        }
    }

    return dict;
}

ProtoObject* py_dict_to_proto_object(PyObject* py_dict, ProtoStackObjectProperty* prev)
{
    PyObject *key, *value;
    Py_ssize_t pos = 0;

    Py_ssize_t attrs_size = PyDict_Size(py_dict);
    ProtoStackObjectProperty* attrs_properties = calloc(attrs_size, sizeof(ProtoStackObjectProperty));
    Py_ssize_t current_idx = 0;

    while (PyDict_Next(py_dict, &pos, &key, &value))
    {
        ProtoStackObjectProperty* p = &attrs_properties[current_idx];

        char* s;
        Py_ssize_t size;
        PyBytes_AsStringAndSize(value, &s, &size);

        if (size == 0)
            continue;

        p->prev = prev;
        p->key = *PyBytes_AsString(key);
        p->value = s;
        p->value_size = size;

        prev = p;
        current_idx++;
    }

    ProtoObject* proto_object = proto_object_allocate(prev);
    free(attrs_properties);
    return proto_object;
}

void server_push_screen(struct server_state_t* server, struct client_state_t* client, const char* name)
{
    struct server_screen_t* s = NULL;
    HASH_FIND_STR(server->screens, name, s);
    if (s == NULL)
    {
        client_printf(client, "could not find screen %s\n", name);
        return;
    }

    uint16_t mem_size = 768 + 6144;
    uint16_t mem_ptr = 0x4000;
    uint8_t* mem_data = s->data;

    client_printf(client, "pushing screen %s\n", name);
    server_push_memory(server, client, mem_ptr, mem_data, mem_size);
}

void server_push_memory(struct server_state_t* server, struct client_state_t* client, uint16_t mem_ptr,
    const uint8_t* mem_data, uint16_t mem_size)
{
    while (mem_size)
    {
        uint16_t mem_chunk = 128;
        if (mem_size < mem_chunk)
        {
            mem_chunk = mem_size;
        }

        declare_arg_property_on_stack(p, 'p', mem_ptr, NULL);
        declare_variable_property_on_stack(screen, 's', mem_data, mem_chunk, &p);
        uint8_t command = MSG_MEMORY_PUSH;
        declare_arg_property_on_stack(id, OBJ_PROPERTY_ID, command, &screen);

        client_state_send_proto_one_object(server, client, &id);

        mem_size -= mem_chunk;
        mem_ptr += mem_chunk;
        mem_data += mem_chunk;
    }
}

void server_push_memory_diff(struct server_state_t* server, struct client_state_t* client,
    const uint8_t* mem_data, uint16_t mem_size)
{
    declare_variable_property_on_stack(p, 'd', mem_data, mem_size, NULL);
    uint8_t command = MSG_MEMORY_PUSH_DIFF;
    declare_arg_property_on_stack(id, OBJ_PROPERTY_ID, command, &p);
    client_state_send_proto_one_object(server, client, &id);
}

double degrees_to_radians(double degrees)
{
    return degrees * (M_PI / 180.0);
}
