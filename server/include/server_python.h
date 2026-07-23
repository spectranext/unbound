#ifndef __SERVER_PYTHON_H
#define __SERVER_PYTHON_H

#include <inttypes.h>
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <proto_objects.h>
#include "object.h"
#include "block.h"

struct server_python_t
{
    struct server_state_t* server_state;
    PyObject* module;
    PyObject* map_api;
    PyObject* computer_api;
    PyObject* device_api;
    PyObject* block_spawn;
    PyObject* update_map_call;
    PyObject* postponed_touch_class;
    PyObject* new_client_callback;
};

struct server_state_t;
struct client_state_t;
struct map_t;
struct server_map_t;
struct server_object_reference_t;
struct server_map_chunk_t;

extern uint8_t server_python_init(struct server_state_t* state, struct server_python_t* server_python,
    const char* debug_host, int debug_port);
extern void server_python_free(struct server_python_t* server_python);

extern void server_python_update_player(struct server_python_t* server_python, struct client_state_t* client_state);
extern void server_python_update(struct server_python_t* server_python, struct client_state_t* client_state);
extern void server_python_block_notifications(struct server_python_t* server_python, struct client_state_t* client_state, const char* reason);
extern void server_python_unblock_notifications(struct server_python_t* server_python, struct client_state_t* client_state, const char* reason);
extern void server_python_player_touch(struct server_python_t* server_python,
    struct client_state_t* client_state, uint16_t x, uint16_t y,
    uint16_t* scheduled_touch);
extern void server_python_player_hit(struct server_python_t* server_python,
    struct client_state_t* client_state, uint16_t angle);
extern void server_python_player_aim(struct server_python_t* server_python,
    struct client_state_t* client_state, uint16_t aim);
extern uint8_t server_python_player_touch_cancel(struct server_python_t* server_python,
    struct client_state_t* client_state);

extern void server_python_assign_py_callbacks(struct server_python_t* server_python, PyObject* py, struct server_object_reference_t* ref);
extern uint8_t server_python_allocate_client(struct server_python_t* server_python, uint16_t client_id);
extern void server_python_on_new_client(struct server_python_t* server_python, uint16_t client_id, const char* scenario);
extern PyObject* server_python_allocate_py_player(struct server_python_t* server_python, uint16_t client_id, PyObject* py);
extern void server_python_assign_py_player_callbacks(struct server_python_t* server_python, struct client_state_t* client_state);
extern void server_python_set_client_authenticated(struct server_python_t* server_python, struct client_state_t* client_state);
extern PyObject* server_python_allocate_py_object(struct server_python_t* server_python, const char* kind);
extern void server_python_player_resume(struct server_python_t* server_python, struct client_state_t* client_state,
    struct server_object_reference_t* ref);

enum client_object_state_t server_python_get_object_default_state(struct server_python_t* server_python, struct server_object_reference_t* ref);

extern void server_python_sync_object_py(struct server_python_t* server_python, struct server_object_reference_t* ref);
extern void server_python_init_object_py(struct server_python_t* server_python, struct server_object_reference_t* ref);
extern void server_python_update_object_py(struct server_python_t* server_python, struct server_object_reference_t* ref);
extern void server_python_yield_object_state(struct server_python_t* server_python, struct server_object_reference_t* ref);
extern void server_python_yield_team(struct server_python_t* server_python, struct server_object_reference_t* ref);
extern void server_python_damage_object_py(struct server_python_t* server_python, struct server_object_reference_t* ref, int value, char* reason);
extern void server_python_object_on_move(struct server_python_t* server_python, struct server_object_reference_t* ref);
extern void server_python_on_block_collide_object_py(struct server_python_t* server_python, struct server_object_reference_t* ref);
extern void server_python_on_fall_object_py(struct server_python_t* server_python,
    struct server_object_reference_t* ref, int8_t fall_speed);
extern uint8_t server_python_get_team_id_object_py(struct server_python_t* server_python, struct server_object_reference_t* ref);
extern void server_python_object_contact_py(struct server_python_t* server_python,
    struct server_object_reference_t* ref, struct server_object_reference_t* contact);
extern PyObject* server_python_object_serialize(struct server_python_t* server_python,
    struct server_object_reference_t* ref);
extern uint8_t server_python_object_deserialize(struct server_python_t* server_python,
    struct server_object_reference_t* ref, PyObject* data_from);
extern map_object_type_t server_python_object_py_get_type(struct server_python_t* server_python, struct server_object_reference_t* ref);
extern const char* server_python_object_py_get_data_entry(struct server_python_t* server_python, struct server_object_reference_t* ref,
    const char* kind);
extern void server_python_object_free_object_py(struct server_python_t* server_python, struct server_object_reference_t* ref);

extern PyObject* server_python_serialize(struct server_python_t* server_python, PyObject* o);
extern uint8_t server_python_deserialize(struct server_python_t* server_python, PyObject* o, PyObject* data);

extern void server_python_terminal(struct server_python_t* server_python,
    struct client_state_t* client_state, const char* command);

extern void server_python_action(struct server_python_t* server_python,
    struct client_state_t* client_state, const char* command, const uint8_t* payload, uint8_t payload_len);

struct client_auth_result_t
{
    char user_id[64];
    char user_name[64];
    char token[68];
    char error[32];
};

extern uint8_t server_python_client_auth(struct server_python_t* server_python,
    const char* token, struct client_auth_result_t* result);

extern void server_python_map_serialize(struct server_python_t* server_python, const char* path);
extern void server_python_map_deserialize(struct server_python_t* server_python, const char* path);

struct player_query_option_t
{
    char* option;
    union {
        uint8_t icon;
        uint8_t full_icon[9];
    };
    uint8_t has_full_icon;
    uint8_t secondary;
    struct player_query_option_t* next;
};

struct player_query_action_t
{
    char* action;
    struct player_query_action_t* next;
};

struct player_query_result
{
    char* message;
    char* description;
    char* cancel_action;
    uint8_t edit;
    uint8_t flags;
    uint8_t* image;
    uint16_t image_size;
    struct player_query_option_t* options;
    size_t options_count;
    size_t current_option;
    struct player_query_action_t* actions;
    size_t actions_count;
    uint8_t quick_cancel;
};

extern uint8_t server_python_player_query(struct server_python_t* server_python,
    struct client_state_t* client_state, const char* query,
    struct player_query_result* result);
extern uint8_t server_python_player_force_query(struct server_python_t* server_python,
    struct client_state_t* client_state, PyObject* response,
    struct player_query_result* result);
extern uint8_t server_python_player_query_option(struct server_python_t* server_python,
    struct client_state_t* client_state, size_t option, const char* action,
    struct player_query_result* new_result);
extern void server_python_player_free_query_result(struct player_query_result* result);

extern void server_python_player_get_stats(struct server_python_t* server_python,
    struct client_state_t* client_state, uint8_t* health, uint8_t* power, uint8_t* temperature,
    uint8_t* hit_auto, uint8_t* hit_delay, uint16_t* credits, char* default_state, char* building_state);

extern void server_python_client_free(struct server_python_t* server_python, struct client_state_t* client_state);

extern void server_python_map_generate(struct server_python_t* server_python, const char* scenario);
extern void server_python_map_init(struct server_python_t* server_python, const char* scenario);
extern void server_python_map_update(struct server_python_t* server_python);
extern void server_python_map_shutdown(struct server_python_t* server_python);
extern void server_python_map_on_chat_message(struct server_python_t* server_python,
    struct client_state_t* client_state, const char* message);
extern uint8_t server_python_map_refresh(struct server_python_t* server_python);
extern block_t server_python_map_refresh_block_code(struct map_t* map, uint16_t x, uint16_t y,
    uint8_t call_refresh);
extern uint8_t server_python_map_set_py(struct server_state_t* server_state, struct server_map_t* map,
    uint16_t x, uint16_t y, PyObject* py, uint8_t notify);
extern PyObject* server_python_allocate_block(struct server_python_t* server_python, const char* identity,
    ProtoObject* proto);
extern uint8_t server_python_map_call_block_method(struct map_t* map, uint16_t x, uint16_t y, const char* method);
extern void server_python_map_call_block_overlap(struct map_t* map, uint16_t x, uint16_t y,
    struct server_object_reference_t* ref);
extern uint8_t server_python_map_call(PyObject* cb);
extern uint8_t server_python_map_serialize_block(struct server_map_chunk_t* chunk, uint8_t x, uint8_t y,
    ProtoObject** serialize_to);
extern void server_python_map_yield_chunk(struct server_state_t* server_state,
    struct server_python_t* server_python, uint16_t ix, uint16_t iy);
extern void server_python_map_yield_chunks(struct server_state_t* server_state,
    struct server_python_t* server_python);

struct server_network_device_t;
struct server_network_device_session_t;

extern PyObject* server_python_device_get_session_handler(struct server_network_device_t* device);
extern PyObject* server_python_device_api_allocate_session(struct server_python_t* server_pyton,
    struct server_network_device_session_t* session,
    struct server_network_device_t* device, PyObject* handler);
extern void server_python_device_session_api_free(PyObject* session);
extern void server_python_device_session_api_on_data(PyObject* session, const uint8_t* data, size_t len);

typedef struct {
    PyObject_HEAD
    struct server_python_t* server_pyton;
} server_pyton_obj;

typedef struct {
    PyObject_HEAD
    struct server_state_t* server_state;
    uint16_t client_id;
} client_pyton_obj;

typedef struct {
    PyObject_HEAD
    struct server_python_t* server_python;
    uint16_t object_id;
} object_obj;

typedef struct {
    PyObject_HEAD
    struct computer_t* computer;
} computer_obj;

typedef struct {
    PyObject_HEAD
    struct server_network_device_t* device;
} server_network_device_obj;

typedef struct {
    PyObject_HEAD
    struct server_network_device_session_t* session;
} server_device_session_obj;

uint8_t server_python_check_err();

PyObject* computer_destroy_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* computer_session_join_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* computer_session_leave_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* computer_is_powered_on_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* computer_is_first_session_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* computer_set_power_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* computer_get_memory_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* computer_set_memory_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* computer_get_ula_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* computer_set_key_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* computer_get_hostname_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* computer_get_hash_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* computer_set_hostname_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* computer_post_message_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* computer_bind_port_write_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* computer_bind_port_read_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* computer_bind_memory_write_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* computer_bind_memory_read_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* computer_mount_path_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* computer_serialize_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* computer_deserialize_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* computer_reboot_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* computer_nmi_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* computer_load_snapshot_cb(PyObject *callable, PyObject *args, PyObject *kwargs);

PyObject* device_destroy_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* device_listen_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* device_listen_close_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* device_get_hostname_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* device_set_hostname_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* device_post_message_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* device_get_device_id_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* device_connect_to_cb(PyObject *callable, PyObject *args, PyObject *kwargs);

PyObject* device_session_write_data_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* device_session_close_cb(PyObject *callable, PyObject *args, PyObject *kwargs);

PyObject* server_map_api_send_effect_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* server_map_api_schedule_map_refresh_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* server_map_api_schedule_block_method_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* server_map_api_schedule_callback_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* server_map_api_update_block_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* server_map_api_query_objects_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* server_map_api_get_object_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* server_map_api_query_clients_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* server_map_api_get_client_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* server_map_api_query_team_computers_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* server_map_api_spawn_player_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* server_map_api_spawn_object_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* server_map_api_iterate_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* server_map_api_set_block_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* server_map_api_get_block_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* server_map_api_set_new_client_callback_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* server_map_api_get_width_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* server_map_api_get_height_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* server_map_api_shutdown_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* server_map_api_computer_new_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* server_map_api_computer_find_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* server_map_api_device_new_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* server_map_api_add_bullet_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* server_printf_cb(PyObject *callable, PyObject *args, PyObject *kwargs);

PyObject* client_api_notify_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* client_api_disconnect_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* client_api_sync_stats_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* client_api_push_module_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* client_api_list_modules_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* client_api_get_module_prop_int_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* client_api_get_module_prop_str_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* client_api_push_screen_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* client_api_push_memory_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* client_api_module_action_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* client_api_force_query_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* client_api_force_watch_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* client_api_send_chat_message_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* client_api_get_name_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* client_api_get_user_id_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* client_api_set_hit_controls_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* client_api_set_object_control_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* client_api_on_team_set_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* client_api_get_client_object_id_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* client_api_get_control_object_id_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* object_api_set_state_flags_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* object_api_set_object_state_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* object_api_reset_object_state_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* object_api_destroy_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* object_api_jump_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* object_api_move_to_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* object_api_set_location_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* object_api_set_speed_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* object_api_set_motion_profile_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* object_api_set_sprite_offset_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* object_api_looking_left_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* object_api_has_collision_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* object_api_get_speed_x_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* object_api_get_speed_y_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* object_api_get_x_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* object_api_get_y_cb(PyObject *callable, PyObject *args, PyObject *kwargs);
PyObject* object_api_get_id_cb(PyObject *callable, PyObject *args, PyObject *kwargs);


#define DEFINE_COMPUTER_CALLBACK(name, type, inttype, fn) static PyTypeObject type = { \
        PyVarObject_HEAD_INIT(NULL, 0)  \
        .tp_name = "computer.api." # name, \
        .tp_basicsize = sizeof(inttype), \
        .tp_itemsize = 0, \
        .tp_flags = Py_TPFLAGS_DEFAULT, \
        .tp_new = PyType_GenericNew, \
        .tp_call = fn, \
    };

#define DEFINE_DEVICE_CALLBACK(name, type, inttype, fn) static PyTypeObject type = { \
        PyVarObject_HEAD_INIT(NULL, 0)  \
        .tp_name = "device.api." # name, \
        .tp_basicsize = sizeof(inttype), \
        .tp_itemsize = 0, \
        .tp_flags = Py_TPFLAGS_DEFAULT, \
        .tp_new = PyType_GenericNew, \
        .tp_call = fn, \
    };

#define DEFINE_DEVICE_SESSION_CALLBACK(name, type, inttype, fn) static PyTypeObject type = { \
        PyVarObject_HEAD_INIT(NULL, 0)  \
        .tp_name = "device.session.api." # name, \
        .tp_basicsize = sizeof(inttype), \
        .tp_itemsize = 0, \
        .tp_flags = Py_TPFLAGS_DEFAULT, \
        .tp_new = PyType_GenericNew, \
        .tp_call = fn, \
    };

#define DEFINE_SERVER_CALLBACK(name, type, inttype, fn) static PyTypeObject type = { \
        PyVarObject_HEAD_INIT(NULL, 0)  \
        .tp_name = "server.api." # name, \
        .tp_basicsize = sizeof(inttype), \
        .tp_itemsize = 0, \
        .tp_flags = Py_TPFLAGS_DEFAULT, \
        .tp_new = PyType_GenericNew, \
        .tp_call = fn, \
    };

#define ASSIGN_COMPUTER_CALLBACK(o, cb_name, type, inttype) { inttype* cb = PyObject_New(inttype, &type); \
    cb->computer = computer; \
    if (PyObject_SetAttrString(o, cb_name, &cb->ob_base)) \
    { \
        PyErr_Print(); \
        return NULL; \
    }}

#define ASSIGN_DEVICE_CALLBACK(o, cb_name, type, inttype) { inttype* cb = PyObject_New(inttype, &type); \
    cb->device = device; \
    if (PyObject_SetAttrString(o, cb_name, &cb->ob_base)) \
    { \
        PyErr_Print(); \
        return NULL; \
    }}

#define ASSIGN_DEVICE_SESSION_CALLBACK(o, cb_name, type, inttype) { inttype* cb = PyObject_New(inttype, &type); \
    cb->session = session; \
    if (PyObject_SetAttrString(o, cb_name, &cb->ob_base)) \
    { \
        PyErr_Print(); \
        return NULL; \
    }}

#define ASSIGN_SERVER_CALLBACK(o, cb_name, type, inttype) { inttype* cb = PyObject_New(inttype, &type); \
    cb->server_pyton = server_python; \
    if (PyObject_SetAttrString(o, cb_name, &cb->ob_base)) \
    { \
        PyErr_Print(); \
        return -1; \
    }}

#define ASSIGN_CLIENT_CALLBACK(client_state_arg, cb_name, type, inttype) { inttype* cb = PyObject_New(inttype, &type); \
    cb->server_state = client_state_arg->state; \
    cb->client_id = client_state_arg->client_id; \
    if (PyObject_SetAttrString(client_state_arg->py, cb_name, &cb->ob_base)) \
    { \
        PyErr_Print(); \
    }}

#define ASSIGN_OBJECT_CALLBACK(object_id, server_python, o, cb_name, type, inttype) { inttype* cb = PyObject_New(inttype, &type); \
    cb->object_id = object_id; \
    cb->server_python = server_python; \
    if (PyObject_SetAttrString(o, cb_name, &cb->ob_base)) \
    { \
        PyErr_Print(); \
    }}

DEFINE_COMPUTER_CALLBACK("ComputerAPI.destroy", computer_destroy_Type, computer_obj, computer_destroy_cb)
DEFINE_COMPUTER_CALLBACK("ComputerAPI.session_join", computer_session_join_Type, computer_obj, computer_session_join_cb)
DEFINE_COMPUTER_CALLBACK("ComputerAPI.session_leave", computer_session_leave_Type, computer_obj, computer_session_leave_cb)
DEFINE_COMPUTER_CALLBACK("ComputerAPI.is_powered_on", computer_is_powered_on_Type, computer_obj, computer_is_powered_on_cb)
DEFINE_COMPUTER_CALLBACK("ComputerAPI.is_first_session", computer_is_first_session_Type, computer_obj, computer_is_first_session_cb)
DEFINE_COMPUTER_CALLBACK("ComputerAPI.set_power", computer_set_power_Type, computer_obj, computer_set_power_cb)
DEFINE_COMPUTER_CALLBACK("ComputerAPI.get_memory", computer_get_memory_Type, computer_obj, computer_get_memory_cb)
DEFINE_COMPUTER_CALLBACK("ComputerAPI.set_memory", computer_set_memory_Type, computer_obj, computer_set_memory_cb)
DEFINE_COMPUTER_CALLBACK("ComputerAPI.get_ula", computer_get_ula_Type, computer_obj, computer_get_ula_cb)
DEFINE_COMPUTER_CALLBACK("ComputerAPI.set_key", computer_set_key_Type, computer_obj, computer_set_key_cb)
DEFINE_COMPUTER_CALLBACK("ComputerAPI.get_hostname", computer_get_hostname_Type, computer_obj, computer_get_hostname_cb)
DEFINE_COMPUTER_CALLBACK("ComputerAPI.get_hash", computer_get_hash_Type, computer_obj, computer_get_hash_cb)
DEFINE_COMPUTER_CALLBACK("ComputerAPI.set_hostname", computer_set_hostname_Type, computer_obj, computer_set_hostname_cb)
DEFINE_COMPUTER_CALLBACK("ComputerAPI.post_message", computer_post_message_Type, computer_obj, computer_post_message_cb)
DEFINE_COMPUTER_CALLBACK("ComputerAPI.bind_port_write", computer_bind_port_write_Type, computer_obj, computer_bind_port_write_cb)
DEFINE_COMPUTER_CALLBACK("ComputerAPI.bind_port_read", computer_bind_port_read_Type, computer_obj, computer_bind_port_read_cb)
DEFINE_COMPUTER_CALLBACK("ComputerAPI.bind_memory_write", computer_bind_memory_write_Type, computer_obj, computer_bind_memory_write_cb)
DEFINE_COMPUTER_CALLBACK("ComputerAPI.bind_memory_read", computer_bind_memory_read_Type, computer_obj, computer_bind_memory_read_cb)
DEFINE_COMPUTER_CALLBACK("ComputerAPI.mount_path", computer_mount_path_Type, computer_obj, computer_mount_path_cb)
DEFINE_COMPUTER_CALLBACK("ComputerAPI.serialize", computer_serialize_Type, computer_obj, computer_serialize_cb)
DEFINE_COMPUTER_CALLBACK("ComputerAPI.deserialize", computer_deserialize_Type, computer_obj, computer_deserialize_cb)
DEFINE_COMPUTER_CALLBACK("ComputerAPI.reboot", computer_reboot_Type, computer_obj, computer_reboot_cb)
DEFINE_COMPUTER_CALLBACK("ComputerAPI.nmi", computer_nmi_Type, computer_obj, computer_nmi_cb)
DEFINE_COMPUTER_CALLBACK("ComputerAPI.load_snapshot", computer_load_snapshot_Type, computer_obj, computer_load_snapshot_cb)

extern void server_python_computer_notify_event(struct computer_t* computer, const char* event);
extern void server_python_computer_notify_log_message(struct computer_t* computer, const char* message);

DEFINE_DEVICE_CALLBACK("DeviceAPI.destroy", device_destroy_Type, server_network_device_obj, device_destroy_cb)
DEFINE_DEVICE_CALLBACK("DeviceAPI.listen", device_listen_Type, server_network_device_obj, device_listen_cb)
DEFINE_DEVICE_CALLBACK("DeviceAPI.listen_close", device_listen_close_Type, server_network_device_obj, device_listen_close_cb)
DEFINE_DEVICE_CALLBACK("DeviceAPI.get_hostname", device_get_hostname_Type, server_network_device_obj, device_get_hostname_cb)
DEFINE_DEVICE_CALLBACK("DeviceAPI.set_hostname", device_set_hostname_Type, server_network_device_obj, device_set_hostname_cb)
DEFINE_DEVICE_CALLBACK("DeviceAPI.post_message", device_post_message_Type, server_network_device_obj, device_post_message_cb)
DEFINE_DEVICE_CALLBACK("DeviceAPI.get_device_id", device_get_device_id_Type, server_network_device_obj, device_get_device_id_cb)
DEFINE_DEVICE_CALLBACK("DeviceAPI.connect_to", device_connect_to_Type, server_network_device_obj, device_connect_to_cb)

DEFINE_DEVICE_SESSION_CALLBACK("DeviceSessionAPI.write_data", device_session_write_data_Type, server_device_session_obj, device_session_write_data_cb)
DEFINE_DEVICE_SESSION_CALLBACK("DeviceSessionAPI.stop", device_session_cose_Type, server_device_session_obj, device_session_close_cb)

DEFINE_SERVER_CALLBACK("MapApi.set_block", server_map_api_set_block_Type, server_pyton_obj, server_map_api_set_block_cb)
DEFINE_SERVER_CALLBACK("MapApi.update_block", server_map_api_update_block_Type, server_pyton_obj, server_map_api_update_block_cb)
DEFINE_SERVER_CALLBACK("MapApi.get_block", server_map_api_get_block_Type, server_pyton_obj, server_map_api_get_block_cb)
DEFINE_SERVER_CALLBACK("MapApi.get_width", server_map_api_get_width_Type, server_pyton_obj, server_map_api_get_width_cb)
DEFINE_SERVER_CALLBACK("MapApi.get_height", server_map_api_get_height_Type, server_pyton_obj, server_map_api_get_height_cb)
DEFINE_SERVER_CALLBACK("MapApi.shutdown", server_map_api_shutdown_Type, server_pyton_obj, server_map_api_shutdown_cb)
DEFINE_SERVER_CALLBACK("MapApi.set_new_client_callback", server_map_api_set_new_client_callback_Type, server_pyton_obj, server_map_api_set_new_client_callback_cb)
DEFINE_SERVER_CALLBACK("MapApi.iterate", server_map_api_iterate_Type, server_pyton_obj, server_map_api_iterate_cb)
DEFINE_SERVER_CALLBACK("MapApi.schedule_map_refresh", server_map_api_schedule_map_refresh_Type, server_pyton_obj, server_map_api_schedule_map_refresh_cb)
DEFINE_SERVER_CALLBACK("MapApi.schedule_block_refresh", server_map_api_schedule_block_method_Type, server_pyton_obj, server_map_api_schedule_block_method_cb)
DEFINE_SERVER_CALLBACK("MapApi.schedule_callback", server_map_api_schedule_callback_Type, server_pyton_obj, server_map_api_schedule_callback_cb)
DEFINE_SERVER_CALLBACK("MapApi.send_effect", server_map_api_send_effect_Type, server_pyton_obj, server_map_api_send_effect_cb)
DEFINE_SERVER_CALLBACK("MapApi.spawn_object", server_map_api_spawn_object_Type, server_pyton_obj, server_map_api_spawn_object_cb)
DEFINE_SERVER_CALLBACK("MapApi.spawn_player", server_map_api_spawn_player_Type, server_pyton_obj, server_map_api_spawn_player_cb)
DEFINE_SERVER_CALLBACK("MapApi.query_objects", server_map_api_query_objects_Type, server_pyton_obj, server_map_api_query_objects_cb)
DEFINE_SERVER_CALLBACK("MapApi.get_object", server_map_api_get_object_Type, server_pyton_obj, server_map_api_get_object_cb)
DEFINE_SERVER_CALLBACK("MapApi.query_clients", server_map_api_query_clients_Type, server_pyton_obj, server_map_api_query_clients_cb)
DEFINE_SERVER_CALLBACK("MapApi.get_client", server_map_api_get_client_Type, server_pyton_obj, server_map_api_get_client_cb)
DEFINE_SERVER_CALLBACK("MapApi.query_team_computers", server_map_api_query_team_computers_Type, server_pyton_obj, server_map_api_query_team_computers_cb)
DEFINE_SERVER_CALLBACK("MapApi.computer_new", server_map_api_computer_new_Type, server_pyton_obj, server_map_api_computer_new_cb)
DEFINE_SERVER_CALLBACK("MapApi.computer_find", server_map_api_computer_find_Type, server_pyton_obj, server_map_api_computer_find_cb)
DEFINE_SERVER_CALLBACK("MapApi.device_new", server_map_api_device_new_Type, server_pyton_obj, server_map_api_device_new_cb)
DEFINE_SERVER_CALLBACK("MapApi.add_bullet", server_map_api_add_bullet_Type, server_pyton_obj, server_map_api_add_bullet_cb)
DEFINE_SERVER_CALLBACK("print", server_print_Type, server_pyton_obj, server_printf_cb)

DEFINE_SERVER_CALLBACK("ClientAPI.notify", client_api_notify_Type, client_pyton_obj, client_api_notify_cb)
DEFINE_SERVER_CALLBACK("ClientAPI.disconnect", client_api_disconnect_Type, client_pyton_obj, client_api_disconnect_cb)
DEFINE_SERVER_CALLBACK("ClientAPI.sync_stats", client_api_sync_stats_Type, client_pyton_obj, client_api_sync_stats_cb)
DEFINE_SERVER_CALLBACK("ClientAPI.force_query", client_api_force_query_Type, client_pyton_obj, client_api_force_query_cb)
DEFINE_SERVER_CALLBACK("ClientAPI.send_chat_message", client_api_send_chat_message_Type, client_pyton_obj, client_api_send_chat_message_cb)
DEFINE_SERVER_CALLBACK("ClientAPI.push_module", client_api_push_module_Type, client_pyton_obj, client_api_push_module_cb)
DEFINE_SERVER_CALLBACK("ClientAPI.list_modules", client_api_list_modules_Type, client_pyton_obj, client_api_list_modules_cb)
DEFINE_SERVER_CALLBACK("ClientAPI.get_module_prop_int", client_api_get_module_prop_int_Type, client_pyton_obj, client_api_get_module_prop_int_cb)
DEFINE_SERVER_CALLBACK("ClientAPI.get_module_prop_str", client_api_get_module_prop_str_Type, client_pyton_obj, client_api_get_module_prop_str_cb)
DEFINE_SERVER_CALLBACK("ClientAPI.push_screen", client_api_push_screen_Type, client_pyton_obj, client_api_push_screen_cb)
DEFINE_SERVER_CALLBACK("ClientAPI.push_memory", client_api_push_memory_Type, client_pyton_obj, client_api_push_memory_cb)
DEFINE_SERVER_CALLBACK("ClientAPI.module_action", client_api_module_action_Type, client_pyton_obj, client_api_module_action_cb)
DEFINE_SERVER_CALLBACK("ClientAPI.force_watch", client_api_force_watch_Type, client_pyton_obj, client_api_force_watch_cb)
DEFINE_SERVER_CALLBACK("ClientAPI.get_name", client_api_get_name_Type, client_pyton_obj, client_api_get_name_cb)
DEFINE_SERVER_CALLBACK("ClientAPI.get_user_id", client_api_get_user_id_Type, client_pyton_obj, client_api_get_user_id_cb)
DEFINE_SERVER_CALLBACK("ClientAPI.set_object_control", client_set_object_control_Type, client_pyton_obj, client_api_set_object_control_cb)
DEFINE_SERVER_CALLBACK("ClientAPI.on_team_set", client_on_team_set_Type, client_pyton_obj, client_api_on_team_set_cb)
DEFINE_SERVER_CALLBACK("ClientAPI.get_client_object_id", client_get_client_object_id_Type, client_pyton_obj, client_api_get_client_object_id_cb)
DEFINE_SERVER_CALLBACK("ClientAPI.get_control_object_id", client_get_control_object_id_Type, client_pyton_obj, client_api_get_control_object_id_cb)

DEFINE_SERVER_CALLBACK("ObjectAPI.looking_left", object_api_looking_left_Type, object_obj, object_api_looking_left_cb)
DEFINE_SERVER_CALLBACK("ObjectAPI.has_collision", object_api_has_collision_Type, object_obj, object_api_has_collision_cb)
DEFINE_SERVER_CALLBACK("ObjectAPI.get_x", object_api_get_x_Type, object_obj, object_api_get_x_cb)
DEFINE_SERVER_CALLBACK("ObjectAPI.get_y", object_api_get_y_Type, object_obj, object_api_get_y_cb)
DEFINE_SERVER_CALLBACK("ObjectAPI.get_id", object_api_get_id_Type, object_obj, object_api_get_id_cb)
DEFINE_SERVER_CALLBACK("ObjectAPI.get_speed_x", object_api_get_speed_x_Type, object_obj, object_api_get_speed_x_cb)
DEFINE_SERVER_CALLBACK("ObjectAPI.get_speed_y", object_api_get_speed_y_Type, object_obj, object_api_get_speed_y_cb)
DEFINE_SERVER_CALLBACK("ObjectAPI.move_to", object_api_move_to_Type, object_obj, object_api_move_to_cb)
DEFINE_SERVER_CALLBACK("ObjectAPI.jump", object_api_jump_Type, object_obj, object_api_jump_cb)
DEFINE_SERVER_CALLBACK("ObjectAPI.set_location", object_api_set_location_Type, object_obj, object_api_set_location_cb)
DEFINE_SERVER_CALLBACK("ObjectAPI.set_speed", object_api_set_speed_Type, object_obj, object_api_set_speed_cb)
DEFINE_SERVER_CALLBACK("ObjectAPI.set_motion_profile", object_api_set_motion_profile_Type, object_obj, object_api_set_motion_profile_cb)
DEFINE_SERVER_CALLBACK("ObjectAPI.set_sprite_offset", object_api_set_sprite_offset_Type, object_obj, object_api_set_sprite_offset_cb)
DEFINE_SERVER_CALLBACK("ObjectAPI.set_state_flags", object_api_set_state_flags_Type, object_obj, object_api_set_state_flags_cb)
DEFINE_SERVER_CALLBACK("ObjectAPI.set_object_state", object_api_set_object_state_Type, object_obj, object_api_set_object_state_cb)
DEFINE_SERVER_CALLBACK("ObjectAPI.reset_object_state", object_api_reset_object_state_Type, object_obj, object_api_reset_object_state_cb)
DEFINE_SERVER_CALLBACK("ObjectAPI.destroy", object_api_destroy_Type, object_obj, object_api_destroy_cb)

#endif
