#ifndef __COMPUTER_H
#define __COMPUTER_H

#include "Z80.h"
#include "computer/w5100_internals.h"
#include "computer/spectranext.h"
#include "computer/xfs.h"
#include "network.h"
#include "server.h"
#include <ut/uthash.h>
#include <stdint.h>
#include <pthread.h>

#define SPECTRANEXT_STDOUT_BUFFER_SIZE 1024

struct client_state_t;
struct server_state_t;

struct keyboard_queue_t
{
    uint8_t row;
    uint8_t key;

    struct keyboard_queue_t* prev;
    struct keyboard_queue_t* next;
};

#define MEM_PAGE_SIZE               4096

// 4 pages of rom
#define MEM_PAGE_ROM                0
// 12 pages of ram
#define MEM_PAGE_RAM                4
// 32 page of spectranet ram
#define MEM_PAGE_SPECTRANET_RAM     16
// 32 page of spectranet ram
#define MEM_PAGE_SPECTRANET_ROM     48
// scratch area
#define MEM_PAGE_SCRATCH            80

#define MEM_PAGES_TOTAL             81

struct computer_state_t
{
    uint8_t                     memory[MEM_PAGES_TOTAL * MEM_PAGE_SIZE];
    uint8_t                     memory_page_read_map[16];
    uint8_t                     memory_page_write_map[16];
    uint8_t                     ula_read[256];
    uint8_t                     ula_write;

    uint8_t                     w5100_page_a;
    uint8_t                     w5100_page_b;
    uint8_t                     flash_page_a;
    uint8_t                     flash_page_b;

    uint8_t                     spectranet_paged_in;
    uint8_t                     spectranet_paged_in_io;
    uint8_t                     spectranet_page_a;
    uint8_t                     spectranet_page_b;
    uint8_t                     spectranet_trap;
    uint8_t                     spectranet_trap_msb;
    uint16_t                    spectranet_trap_pc;
    uint8_t                     spectranet_nmi_flip_flop;
    uint8_t                     spectranet_flash_state;
    uint8_t                     spectranet_read_device_id;
};

struct __attribute__((__packed__)) computer_snapshot_t
{
    struct computer_state_t     state;

    struct
    {
        ZInt32 data;
        ZInt16 ix_iy[2];
        ZInt16 pc;
        ZInt16 sp;
        ZInt16 xy;
        ZInt16 memptr;
        ZInt16 af;
        ZInt16 bc;
        ZInt16 de;
        ZInt16 hl;
        ZInt16 af_;
        ZInt16 bc_;
        ZInt16 de_;
        ZInt16 hl_;
        zuint8 r;
        zuint8 i;
        zuint8 r7;
        zuint8 im;
        zuint8 request;
        zuint8 resume;
        zuint8 iff1;
        zuint8 iff2;
        zuint8 q;
    }   z80;
};

typedef void (*port_write_cb_t)(void* user, uint8_t value);
typedef uint8_t (*port_read_cb_t)(void* user);
typedef void (*memory_write_cb_t)(void* user, uint16_t offset, uint8_t value);
typedef uint8_t (*memory_read_cb_t)(void* user, uint16_t offset);
typedef int16_t (*xfs_path_open_cb_t)(void* user, const char* path, int flags,
    uint8_t is_dir, void** out_handle);
typedef int16_t (*xfs_path_read_cb_t)(void* user, void* handle, uint8_t* buffer, uint16_t size);
typedef int16_t (*xfs_path_write_cb_t)(void* user, void* handle, const uint8_t* buffer, uint16_t size);
typedef int16_t (*xfs_path_close_cb_t)(void* user, void* handle);
typedef int32_t (*xfs_path_seek_cb_t)(void* user, void* handle, uint8_t mode, uint32_t offset);
typedef int16_t (*xfs_path_readdir_cb_t)(void* user, void* handle, struct xfs_stat_info* info);

struct computer_port_write_binds_t
{
    int                         address;
    port_write_cb_t             callback;
    main_thread_runnable_cb     released;
    void*                       user;
    UT_hash_handle              hh;
};

struct computer_port_read_binds_t
{
    int                         address;
    port_read_cb_t              callback;
    main_thread_runnable_cb     released;
    void*                       user;
    UT_hash_handle              hh;
};

struct computer_memory_write_binds_t
{
    uint16_t                    address;
    uint16_t                    size;
    memory_write_cb_t           callback;
    main_thread_runnable_cb     released;
    void*                       user;
    struct computer_memory_write_binds_t* prev;
    struct computer_memory_write_binds_t* next;
};

struct computer_memory_read_binds_t
{
    uint16_t                    address;
    uint16_t                    size;
    memory_read_cb_t            callback;
    main_thread_runnable_cb     released;
    void*                       user;
    struct computer_memory_read_binds_t* prev;
    struct computer_memory_read_binds_t* next;
};

struct computer_xfs_path_bind_t
{
    char                        path[PATH_MAX];
    uint8_t                     has_readdir;
    xfs_path_open_cb_t          open;
    xfs_path_read_cb_t          read;
    xfs_path_write_cb_t         write;
    xfs_path_close_cb_t         close;
    xfs_path_seek_cb_t          seek;
    xfs_path_readdir_cb_t       readdir;
    main_thread_runnable_cb     released;
    void*                       user;
    UT_hash_handle              hh;
};

struct computer_t /* -> struct network_device_t */
{
    /* must be first */
    struct network_device_t     device;
    char*                       hash;

    Z80                         cpu;
    struct computer_state_t     state;
    struct nic_w5100_t          spectranet_w5100;
    struct spectranext_controller_t spectranext_controller;
    struct spectranext_state_t  spectranext_state;
    struct xfs_state_t          xfs;
    uint8_t                     ula_write_dirty;
    uint8_t                     running;
    uint8_t                     first_session;
    pthread_t                   thread;
    pthread_mutex_t             run_mutex;
    int                         num_sessions;
    int                         frame_counter;
    uint8_t                     modified_screen[768];
    uint8_t                     modified_screen_flag;
    uint8_t                     int_buffer[2000];

    long                        sampling_time;
    struct client_state_t*      active_session;
    struct keyboard_queue_t*    keyboard_queue;
    struct server_state_t*      server_state;

    struct server_main_thread_runnable_wait post_wait;

    struct computer_port_write_binds_t* write_binds;
    struct computer_port_read_binds_t* read_binds;
    struct computer_memory_write_binds_t* memory_write_binds;
    struct computer_memory_read_binds_t* memory_read_binds;
    struct computer_xfs_path_bind_t* xfs_path_binds;

    PyObject*                   computer_api;
    char                        stdout_buffer[SPECTRANEXT_STDOUT_BUFFER_SIZE];
    size_t                      stdout_buffer_length;

    UT_hash_handle              hh;
    UT_hash_handle              hash_hh;
};

extern uint8_t computer_rom_load(struct computer_rom_t* rom, const char* filename);
extern uint8_t computer_snapshot_load(struct computer_t* computer, const uint8_t* data, uint32_t size);

extern void computer_init(struct computer_t* computer, uint8_t namespace_id, struct server_state_t* server_state);
extern void computer_serialize(struct computer_t* computer, struct computer_snapshot_t* snapshot);
extern void computer_deserialize(struct computer_t* computer, struct computer_snapshot_t* snapshot);
extern void computer_destroy(struct computer_t* computer);
extern void computer_start(struct computer_t* computer);
extern void computer_stop(struct computer_t* computer);
extern void computer_reboot(struct computer_t* computer);
extern void computer_nmi(struct computer_t* computer);
extern void computer_set_ula(struct computer_t* computer, uint8_t addr, uint8_t value);
extern uint8_t computer_get_ula_byte(struct computer_t* computer);

extern void computer_bind_port_write(struct computer_t* computer, uint16_t address, port_write_cb_t cb,
    main_thread_runnable_cb released, void* user);
extern void computer_bind_port_read(struct computer_t* computer, uint16_t address, port_read_cb_t cb,
    main_thread_runnable_cb released, void* user);
extern void computer_bind_memory_write(struct computer_t* computer, uint16_t address, uint16_t size,
    memory_write_cb_t cb, main_thread_runnable_cb released, void* user);
extern void computer_bind_memory_read(struct computer_t* computer, uint16_t address, uint16_t size,
    memory_read_cb_t cb, main_thread_runnable_cb released, void* user);
extern uint8_t computer_mount_path(struct computer_t* computer, const char* path, uint8_t has_readdir,
    xfs_path_open_cb_t open, xfs_path_read_cb_t read, xfs_path_write_cb_t write,
    xfs_path_close_cb_t close, xfs_path_seek_cb_t seek, xfs_path_readdir_cb_t readdir,
    main_thread_runnable_cb released, void* user);
extern struct computer_xfs_path_bind_t* computer_find_xfs_path_bind(struct computer_t* computer, const char* path);
extern uint8_t computer_xfs_path_has_children(struct computer_t* computer, const char* path);
extern uint8_t computer_write_port(struct computer_t* computer, uint16_t address, uint8_t value);
extern uint8_t computer_read_port(struct computer_t* computer, uint16_t address, uint8_t* result);
extern uint8_t computer_write_bound_memory(struct computer_t* computer, uint16_t address, uint8_t value);
extern uint8_t computer_read_bound_memory(struct computer_t* computer, uint16_t address, uint8_t* result);

extern void computer_spectranet_refresh_pages(struct computer_t* computer);

extern void computer_read_memory(struct computer_t* computer, uint32_t address, uint8_t* data, uint16_t size);
extern void computer_write_memory(struct computer_t* computer, uint32_t address, const uint8_t* data, uint16_t size);

extern void computer_session_key_action(struct computer_t* computer, uint8_t row, uint8_t key);
extern uint8_t computer_session_join(struct computer_t* computer, struct client_state_t* session);
extern uint8_t computer_session_leave(struct computer_t* computer);

extern void spectranet_page_in(struct computer_t* computer, uint8_t io);
extern void spectranet_page_out(struct computer_t* computer);
extern void spectranet_flash_write(struct computer_t* computer, uint8_t page, uint16_t address, uint8_t val);
extern uint8_t spectranet_w5100_read(struct computer_t* computer, uint8_t page, uint16_t address);
extern void spectranet_w5100_write(struct computer_t* computer, uint8_t page, uint16_t address, uint8_t b);
extern void spectranet_check_pc_pre_fetch(struct computer_t* computer, zuint16 address);
extern void spectranet_check_pc_post_fetch(struct computer_t* computer);
extern void spectranet_set_page_a(struct computer_t* computer, uint8_t val);
extern void spectranet_set_page_b(struct computer_t* computer, uint8_t val);
extern void spectranet_enable_programmable_trap(struct computer_t* computer, uint8_t trap);
extern void spectranet_supply_trap_pc(struct computer_t* computer, uint8_t val);

#endif
