#ifndef __UTILS_H__
#define __UTILS_H__

#include <stdint.h>
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <proto_objects.h>

extern uint8_t* hex_to_bytes(char* string, uint16_t* out_array_len);
extern uint8_t file_exists(const char *filename);

extern PyObject* proto_object_to_py_dict(ProtoObject* proto_object);
extern ProtoObject* py_dict_to_proto_object(PyObject* py_dict, ProtoStackObjectProperty* prev);

struct server_state_t;
struct client_state_t;

extern void server_push_screen(struct server_state_t* server, struct client_state_t* client, const char* name);
extern void server_push_memory(struct server_state_t* server, struct client_state_t* client, uint16_t mem_ptr,
    const uint8_t* mem_data, uint16_t mem_size);
extern void server_push_memory_diff(struct server_state_t* server, struct client_state_t* client,
    const uint8_t* mem_data, uint16_t mem_size);

extern double degrees_to_radians(double degrees);

#endif