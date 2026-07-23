#include "computer.h"
#include "server.h"
#include <ut/utlist.h>
#include <strings.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include "utils.h"
#include "computer/w5100.h"
#include "computer/spectranext.h"
#include "computer/xfs.h"
#include "server_python.h"
#include "network.h"
#include "messages.h"

#define CYCLES_AT_INT    24
#define CYCLES_PER_INT   32

static void VideoAddressToXY(uint16_t address, uint16_t* x, uint16_t* y);
static uint16_t XYToVideoAddress(uint16_t x_, uint16_t y_);
static uint8_t* encode_image_row(struct computer_t* computer, uint8_t* out_ptr, uint16_t x, uint16_t y, uint8_t len);

static uint8_t computer_get_video_diff(struct computer_t* computer);
static void computer_keyboard_queue_free(struct computer_t* computer);

static void* computer_thread(void* ctx);
static zuint8 z80_nmi_read(void *context, zuint16 address);
static void z80_retn(void *context);
static zuint8 z80_memory_read(void *context, zuint16 address);
static zuint8 z80_memory_fetch(void *context, zuint16 address);
static zuint8 z80_memory_fetch_opcode(void *context, zuint16 address);
static void z80_memory_write(void *context, zuint16 address, zuint8 value);
static zuint8 z80_int_fetch_read(void *context, zuint16 address);
static zuint8 z80_io_read(void *context, zuint16 address);
static void z80_io_write(void *context, zuint16 address, zuint8 value);

void computer_init(struct computer_t* computer, uint8_t namespace_id, struct server_state_t* server_state)
{
    memset(computer, 0, sizeof(struct computer_t));

    network_device_init(&computer->device, namespace_id, DEVICE_COMPUTER, &server_state->network_bindings);

    char hostname[128];
    sprintf(hostname, "cpu%d", computer->device.device_id);
    network_device_assign_hostname(&computer->device, hostname);

    computer->state.memory_page_write_map[1] = MEM_PAGE_SCRATCH;
    computer->state.memory_page_write_map[2] = MEM_PAGE_SCRATCH;
    computer->state.memory_page_write_map[4] = MEM_PAGE_SCRATCH;

    computer->state.memory_page_read_map[1] = MEM_PAGE_ROM + 1;
    computer->state.memory_page_read_map[2] = MEM_PAGE_ROM + 2;
    computer->state.memory_page_read_map[3] = MEM_PAGE_ROM + 3;

    for (int i = 0; i < 16; i++)
    {
        computer->state.memory_page_read_map[i] = i;

        if (i < 4)
        {
            // rom
            computer->state.memory_page_write_map[i] = MEM_PAGE_SCRATCH;
        }
        else
        {
            // ram
            computer->state.memory_page_write_map[i] = i;
        }
    }

    computer->cpu.context = computer;
    computer->cpu.fetch = z80_memory_fetch;
    computer->cpu.fetch_opcode = z80_memory_fetch_opcode;
    computer->cpu.read = z80_memory_read;
    computer->cpu.write = z80_memory_write;
    computer->cpu.in = z80_io_read;
    computer->cpu.out = z80_io_write;
    computer->cpu.int_fetch = z80_int_fetch_read;
    computer->cpu.nmia = z80_nmi_read;
    computer->cpu.retn = z80_retn;
    computer->cpu.options = Z80_OPTION_HALT_SKIP;

    computer->first_session = 0;
    computer->server_state = server_state;
    memset(computer->state.ula_read, 0b11111, sizeof(computer->state.ula_read));
    memcpy(&computer->state.memory[MEM_PAGE_SIZE * MEM_PAGE_ROM], server_state->rom_48k.rom, server_state->rom_48k.size);
    memcpy(&computer->state.memory[MEM_PAGE_SIZE * MEM_PAGE_SPECTRANET_ROM], server_state->rom_spectranet.rom, server_state->rom_spectranet.size);

    computer->state.spectranet_page_a = 0xC0;
    computer->state.spectranet_page_b = 0xC0;

    computer->state.w5100_page_a = 0xFF;
    computer->state.w5100_page_b = 0xFF;
    computer->state.flash_page_a = 0xFF;
    computer->state.flash_page_b = 0xFF;
    spectranext_controller_init(computer);
    xfs_init(computer);

    z80_power(&computer->cpu, Z_TRUE);
    spectranet_page_in(computer, 0);
    pthread_mutex_init(&computer->run_mutex, NULL);

    pthread_mutex_init(&computer->post_wait.mutex, NULL);
    pthread_cond_init(&computer->post_wait.cond, NULL);
}

void computer_serialize(struct computer_t* computer, struct computer_snapshot_t* snapshot)
{
    pthread_mutex_lock(&computer->run_mutex);

    memcpy(&snapshot->state, &computer->state, sizeof(struct computer_state_t));

    snapshot->z80.data = computer->cpu.data;
    memcpy(snapshot->z80.ix_iy, computer->cpu.ix_iy, sizeof(snapshot->z80.ix_iy));
    snapshot->z80.pc = computer->cpu.pc;
    snapshot->z80.sp = computer->cpu.sp;
    snapshot->z80.xy = computer->cpu.xy;
    snapshot->z80.memptr = computer->cpu.memptr;
    snapshot->z80.af = computer->cpu.af;
    snapshot->z80.bc = computer->cpu.bc;
    snapshot->z80.de = computer->cpu.de;
    snapshot->z80.hl = computer->cpu.hl;
    snapshot->z80.af_ = computer->cpu.af_;
    snapshot->z80.bc_ = computer->cpu.bc_;
    snapshot->z80.de_ = computer->cpu.de_;
    snapshot->z80.hl_ = computer->cpu.hl_;
    snapshot->z80.r = computer->cpu.r;
    snapshot->z80.i = computer->cpu.i;
    snapshot->z80.r7 = computer->cpu.r7;
    snapshot->z80.im = computer->cpu.im;
    snapshot->z80.request = computer->cpu.request;
    snapshot->z80.resume = computer->cpu.resume;
    snapshot->z80.iff1 = computer->cpu.iff1;
    snapshot->z80.iff2 = computer->cpu.iff2;
    snapshot->z80.q = computer->cpu.q;

    pthread_mutex_unlock(&computer->run_mutex);
}

void computer_deserialize(struct computer_t* computer, struct computer_snapshot_t* snapshot)
{
    pthread_mutex_lock(&computer->run_mutex);

    memcpy(&computer->state, &snapshot->state, sizeof(struct computer_state_t));

    computer->cpu.data = snapshot->z80.data;
    memcpy(computer->cpu.ix_iy, snapshot->z80.ix_iy, sizeof(snapshot->z80.ix_iy));
    computer->cpu.pc = snapshot->z80.pc;
    computer->cpu.sp = snapshot->z80.sp;
    computer->cpu.xy = snapshot->z80.xy;
    computer->cpu.memptr = snapshot->z80.memptr;
    computer->cpu.af = snapshot->z80.af;
    computer->cpu.bc = snapshot->z80.bc;
    computer->cpu.de = snapshot->z80.de;
    computer->cpu.hl = snapshot->z80.hl;
    computer->cpu.af_ = snapshot->z80.af_;
    computer->cpu.bc_ = snapshot->z80.bc_;
    computer->cpu.de_ = snapshot->z80.de_;
    computer->cpu.hl_ = snapshot->z80.hl_;
    computer->cpu.r = snapshot->z80.r;
    computer->cpu.i = snapshot->z80.i;
    computer->cpu.r7 = snapshot->z80.r7;
    computer->cpu.im = snapshot->z80.im;
    computer->cpu.request = snapshot->z80.request;
    computer->cpu.resume = snapshot->z80.resume;
    computer->cpu.iff1 = snapshot->z80.iff1;
    computer->cpu.iff2 = snapshot->z80.iff2;
    computer->cpu.q = snapshot->z80.q;

    pthread_mutex_unlock(&computer->run_mutex);
}

static zuint8 z80_int_fetch_read(void *context, zuint16 address)
{
    return 0;
}

static zuint8 z80_io_read(void *context, zuint16 address)
{
    struct computer_t* computer = context;

    {
        uint8_t ret;
        if (computer_read_port(computer, address, &ret))
        {
            return ret;
        }
    }

    if ((address & 0xff) == 0xfe) {
        return computer->state.ula_read[(address & 0xff00) >> 8];
    } else if ((address & 0x033B) == 0x033B) {
        uint8_t result = computer->state.ula_write & 0x07;
        if (computer->state.spectranet_trap) {
            result |= 0x08;
        }
        return result;
    }

    return 0xFF;
}

static void z80_io_write(void *context, zuint16 address, zuint8 value)
{
    struct computer_t* computer = context;

    if (computer_write_port(computer, address, value))
    {
        return;
    }

    if ((address & 0xff) == 0xfe) {
        if (value != computer->state.ula_write)
        {
            computer->state.ula_write = value;
            computer->ula_write_dirty = 1;
        }
        return;
    } else if (address == 0x043b) {
        spectranext_stdout_write(computer, address, value);
        return;
    } else if ((address & 0x033B) == 0x033B) {
        if (value & 0x01) {
            spectranet_page_in(computer, 1);
        } else if (computer->state.spectranet_paged_in_io) {
            spectranet_page_out(computer);
        }
        spectranet_enable_programmable_trap(computer, value & 0x08);
    } else if ((address & 0x023B) == 0x023B) {
        spectranet_supply_trap_pc(computer, value);
    } else if ((address & 0x013B) == 0x013B) {
        spectranet_set_page_b(computer, value);
    } else if ((address & 0x3B) == 0x3B) {
        spectranet_set_page_a(computer, value);
    } else if (!(address & 0x8002)) {
        /* 128k paging */
    } else {
        server_printf("io write %d -> %d\n", address, value);
    }
}

static zuint8 z80_memory_read(void *context, zuint16 address)
{
    struct computer_t* computer = context;
    uint8_t result;

    if (computer_read_bound_memory(computer, address, &result))
    {
        return result;
    }

    uint8_t page_addr = (uint8_t)(address >> 12);
    uint8_t spectranet_page = 0xFF;

    if (computer->state.spectranet_paged_in)
    {
        if (page_addr == 1)
            spectranet_page = computer->state.spectranet_page_a;
        else if (page_addr == 2)
            spectranet_page = computer->state.spectranet_page_b;
    }

    if (spectranet_page == SPECTRANEXT_CONTROLLER_PAGE)
        return spectranext_controller_read(computer, address);

    if (spectranet_page == XFS_SPECTRANET_PAGE)
        return xfs_read(computer, address);

    if ((computer->state.w5100_page_a != 0xFF) && page_addr == 1) {
        return spectranet_w5100_read(computer, computer->state.w5100_page_a, address);
    }
    if ((computer->state.w5100_page_b != 0xFF) && page_addr == 2) {
        return spectranet_w5100_read(computer, computer->state.w5100_page_b, address);
    }

    if (computer->state.spectranet_read_device_id && address < 0x4000) {
        computer->state.spectranet_read_device_id = 0;
        return 0x00;
    }

    uint8_t page = computer->state.memory_page_read_map[page_addr];
    uint32_t pageStartPtr = ((uint32_t)page) << 12;

    return computer->state.memory[pageStartPtr | (address & 0x0fff)];
}

static void z80_memory_write(void *context, zuint16 address, zuint8 value)
{
    struct computer_t* computer = context;

    if (computer_write_bound_memory(computer, address, value))
    {
        return;
    }

    uint8_t page_addr = address >> 12;
    uint8_t spectranet_page = 0xFF;

    if (computer->state.spectranet_paged_in)
    {
        if (page_addr == 1)
            spectranet_page = computer->state.spectranet_page_a;
        else if (page_addr == 2)
            spectranet_page = computer->state.spectranet_page_b;
    }

    if (spectranet_page == SPECTRANEXT_CONTROLLER_PAGE)
    {
        spectranext_controller_write(computer, address, value);
        return;
    }

    if (spectranet_page == XFS_SPECTRANET_PAGE)
    {
        xfs_write(computer, address, value);
        return;
    }
    
    if (page_addr == 1) {
        if (computer->state.w5100_page_a != 0xFF) {
            spectranet_w5100_write(computer, computer->state.w5100_page_a, address, value);
            return;
        }
        else if (computer->state.flash_page_a != 0xFF) {
            spectranet_flash_write(computer, computer->state.flash_page_a, address, value);
            return;
        }
    }
    
    if (page_addr == 2) {
        if (computer->state.w5100_page_b != 0xFF) {
            spectranet_w5100_write(computer, computer->state.w5100_page_b, address, value);
            return;
        }
        else if (computer->state.flash_page_b != 0xFF) {
            spectranet_flash_write(computer, computer->state.flash_page_b, address, value);
            return;
        }
    }

    uint8_t page = computer->state.memory_page_write_map[page_addr];
    uint32_t pageStartPtr = ((uint32_t)page) << 12;

    computer->state.memory[pageStartPtr | (address & 0x0fff)] = value;

    if (computer->active_session)
    {
        if (address >= 0x4000 && address < 0x5800)
        {
            uint16_t x, y;
            VideoAddressToXY(address, &x, &y);
            computer->modified_screen[x + y * 32] = 1;
            computer->modified_screen_flag = 1;
        }
        else if (address >= 0x5800 && address < 0x5b00)
        {
            computer->modified_screen[address - 0x5800] = 1;
            computer->modified_screen_flag = 1;
        }
    }
}

void computer_destroy(struct computer_t* computer)
{
    server_python_computer_notify_event(computer, NULL);

    pthread_mutex_lock(&computer->run_mutex);

    {
        struct computer_port_write_binds_t* b;
        struct computer_port_write_binds_t* tmp;

        HASH_ITER(hh, computer->write_binds, b, tmp)
        {
            struct server_main_thread_runnable_args args;
            args.port_write_bind.user = b->user;

            server_state_post_runnable(computer->server_state, b->released, args);
            HASH_DEL(computer->write_binds, b);
            free(b);
        }
    }

    {
        struct computer_port_read_binds_t* b;
        struct computer_port_read_binds_t* tmp;

        HASH_ITER(hh, computer->read_binds, b, tmp)
        {
            struct server_main_thread_runnable_args args;
            args.port_read_bind.user = b->user;

            server_state_post_runnable(computer->server_state, b->released, args);
            HASH_DEL(computer->read_binds, b);
            free(b);
        }
    }

    {
        struct computer_memory_write_binds_t* b;
        struct computer_memory_write_binds_t* tmp;

        DL_FOREACH_SAFE(computer->memory_write_binds, b, tmp)
        {
            struct server_main_thread_runnable_args args;
            args.memory_write_bind.user = b->user;

            server_state_post_runnable(computer->server_state, b->released, args);
            DL_DELETE(computer->memory_write_binds, b);
            free(b);
        }
    }

    {
        struct computer_memory_read_binds_t* b;
        struct computer_memory_read_binds_t* tmp;

        DL_FOREACH_SAFE(computer->memory_read_binds, b, tmp)
        {
            struct server_main_thread_runnable_args args;
            args.memory_read_bind.user = b->user;

            server_state_post_runnable(computer->server_state, b->released, args);
            DL_DELETE(computer->memory_read_binds, b);
            free(b);
        }
    }

    pthread_mutex_unlock(&computer->run_mutex);

    computer_stop(computer);
    xfs_reset(computer);

    {
        struct computer_xfs_path_bind_t* b;
        struct computer_xfs_path_bind_t* tmp;

        HASH_ITER(hh, computer->xfs_path_binds, b, tmp)
        {
            struct server_main_thread_runnable_args args;
            args.xfs_path_bind.user = b->user;

            server_state_post_runnable(computer->server_state, b->released, args);
            HASH_DEL(computer->xfs_path_binds, b);
            free(b);
        }
    }

    computer_keyboard_queue_free(computer);
    pthread_mutex_destroy(&computer->run_mutex);
    network_device_destroy(&computer->device);

    pthread_mutex_destroy(&computer->post_wait.mutex);
    pthread_cond_destroy(&computer->post_wait.cond);

    // this frees itself
    server_state_computer_free(computer->server_state, computer);
}

static void* computer_thread(void* ctx)
{
    struct computer_t* computer = (struct computer_t*)ctx;
    Z80* cpu = &computer->cpu;

    computer->running = 1;

    nic_w5100_init(&computer->spectranet_w5100, computer);

    while (computer->running)
    {
        struct timeval st, et;

        pthread_mutex_lock(&computer->run_mutex);

        // 70000 cycles per frame (1/50 of seconds, or 20000 microseconds)
        // with 7000 cycles, that's 2000 microseconds

        gettimeofday(&st,NULL);
        z80_run(cpu, 7000);
        gettimeofday(&et,NULL);

        computer->frame_counter++;
        if (computer->frame_counter % 10 == 0)
        {
            /* CPU cycles during the INT signal */
            zusize cycles = z80_execute(cpu, CYCLES_AT_INT);
            z80_int(cpu, Z_TRUE);
            z80_run(cpu, (CYCLES_AT_INT + CYCLES_PER_INT) - cycles);
            z80_int(cpu, Z_FALSE);
        }

        {
            struct timeval t;
            gettimeofday(&t,NULL);
            long time = (t.tv_sec * 1000000) + t.tv_usec;
            if (time > computer->sampling_time)
            {
                computer->sampling_time = time + 50000;
                if (computer->keyboard_queue)
                {
                    struct keyboard_queue_t* first = computer->keyboard_queue;
                    computer->state.ula_read[first->row] = first->key;
                    DL_DELETE(computer->keyboard_queue, first);
                    free(first);
                }

                if (computer->active_session)
                {
                    computer_get_video_diff(computer);

                    if (computer->ula_write_dirty)
                    {
                        computer->ula_write_dirty = 0;

                        declare_arg_property_on_stack(p, 'u', computer->state.ula_write, NULL);
                        uint8_t command = MSG_ULA_WRITE;
                        declare_arg_property_on_stack(id, OBJ_PROPERTY_ID, command, &p);
                        client_state_send_proto_one_object(computer->server_state, computer->active_session, &id);
                    }
                }
            }
        }

        pthread_mutex_unlock(&computer->run_mutex);

        long elapsed = ((et.tv_sec - st.tv_sec) * 1000000) + (et.tv_usec - st.tv_usec);
        if (elapsed < 2000)
        {
            usleep(2000 - elapsed);
        }
    }

    nic_w5100_destroy(&computer->spectranet_w5100);

    return NULL;
}

extern void* bindings_process_messages(void* ctx);

void computer_start(struct computer_t* computer)
{
    if (computer->thread)
    {
        return;
    }

    server_printf("starting computer %s (namespace %d)\n", computer->device.hostname, computer->device.namespace_id);
    pthread_create(&computer->thread, NULL, computer_thread, computer);

    server_python_computer_notify_event(computer, "Computer started");
}

void computer_stop(struct computer_t* computer)
{
    if (computer->running)
    {
        server_python_computer_notify_event(computer, "Stopping computer");

        server_printf("stopping computer %s\n", computer->device.hostname);
        computer->running = 0;
        pthread_join(computer->thread, NULL);
        computer->thread = NULL;

        server_printf("computer %s stopped\n", computer->device.hostname);
    }
}

void computer_reboot(struct computer_t* computer)
{
    pthread_mutex_lock(&computer->run_mutex);

    z80_instant_reset(&computer->cpu);
    spectranet_page_in(computer, 0);
    spectranext_controller_init(computer);
    xfs_reset(computer);

    pthread_mutex_unlock(&computer->run_mutex);

    server_python_computer_notify_event(computer, "Computer has been rebooted");
}

void computer_nmi(struct computer_t* computer)
{
    pthread_mutex_lock(&computer->run_mutex);

    computer->state.spectranet_nmi_flip_flop = 1;
    z80_nmi(&computer->cpu);

    pthread_mutex_unlock(&computer->run_mutex);

    server_python_computer_notify_event(computer, "Magic button pressed");
}

void computer_set_ula(struct computer_t* computer, uint8_t address, uint8_t value)
{
    pthread_mutex_lock(&computer->run_mutex);
    computer->state.ula_read[address] = value;
    pthread_mutex_unlock(&computer->run_mutex);
}

uint8_t computer_get_ula_byte(struct computer_t* computer)
{
    pthread_mutex_lock(&computer->run_mutex);
    uint8_t result = computer->state.ula_write;
    pthread_mutex_unlock(&computer->run_mutex);
    return result;
}

void computer_bind_port_write(struct computer_t* computer, uint16_t address, port_write_cb_t cb,
    main_thread_runnable_cb released, void* user)
{
    pthread_mutex_lock(&computer->run_mutex);

    struct computer_port_write_binds_t* b = calloc(1, sizeof(struct computer_port_write_binds_t));
    b->user = user;
    b->callback = cb;
    b->released = released;
    b->address = address;

    HASH_ADD_INT(computer->write_binds, address, b);

    pthread_mutex_unlock(&computer->run_mutex);
}

void computer_bind_port_read(struct computer_t* computer, uint16_t address, port_read_cb_t cb,
    main_thread_runnable_cb released, void* user)
{
    pthread_mutex_lock(&computer->run_mutex);

    struct computer_port_read_binds_t* b = calloc(1, sizeof(struct computer_port_read_binds_t));
    b->user = user;
    b->callback = cb;
    b->released = released;
    b->address = address;

    HASH_ADD_INT(computer->read_binds, address, b);

    pthread_mutex_unlock(&computer->run_mutex);
}

void computer_bind_memory_write(struct computer_t* computer, uint16_t address, uint16_t size,
    memory_write_cb_t cb, main_thread_runnable_cb released, void* user)
{
    pthread_mutex_lock(&computer->run_mutex);

    struct computer_memory_write_binds_t* b = calloc(1, sizeof(struct computer_memory_write_binds_t));
    b->user = user;
    b->callback = cb;
    b->released = released;
    b->address = address;
    b->size = size;

    DL_APPEND(computer->memory_write_binds, b);

    pthread_mutex_unlock(&computer->run_mutex);
}

void computer_bind_memory_read(struct computer_t* computer, uint16_t address, uint16_t size,
    memory_read_cb_t cb, main_thread_runnable_cb released, void* user)
{
    pthread_mutex_lock(&computer->run_mutex);

    struct computer_memory_read_binds_t* b = calloc(1, sizeof(struct computer_memory_read_binds_t));
    b->user = user;
    b->callback = cb;
    b->released = released;
    b->address = address;
    b->size = size;

    DL_APPEND(computer->memory_read_binds, b);

    pthread_mutex_unlock(&computer->run_mutex);
}

static uint8_t normalize_xfs_mount_path(const char* path, char* out, size_t out_size)
{
    size_t len;

    if (!path || !path[0] || path[0] != '/' || out_size < 2)
        return 0;

    snprintf(out, out_size, "%s", path);
    len = strlen(out);
    while (len > 1 && out[len - 1] == '/')
    {
        out[len - 1] = '\0';
        len--;
    }
    return len < out_size;
}

uint8_t computer_mount_path(struct computer_t* computer, const char* path, uint8_t has_readdir,
    xfs_path_open_cb_t open, xfs_path_read_cb_t read, xfs_path_write_cb_t write,
    xfs_path_close_cb_t close, xfs_path_seek_cb_t seek, xfs_path_readdir_cb_t readdir,
    main_thread_runnable_cb released, void* user)
{
    char normalized[PATH_MAX];
    struct computer_xfs_path_bind_t* old = NULL;

    if (!normalize_xfs_mount_path(path, normalized, sizeof(normalized)))
        return 0;

    pthread_mutex_lock(&computer->run_mutex);

    HASH_FIND_STR(computer->xfs_path_binds, normalized, old);
    if (old)
    {
        struct server_main_thread_runnable_args args;
        args.xfs_path_bind.user = old->user;

        server_state_post_runnable(computer->server_state, old->released, args);
        HASH_DEL(computer->xfs_path_binds, old);
        free(old);
    }

    struct computer_xfs_path_bind_t* b = calloc(1, sizeof(*b));
    if (!b)
    {
        pthread_mutex_unlock(&computer->run_mutex);
        return 0;
    }

    snprintf(b->path, sizeof(b->path), "%s", normalized);
    b->has_readdir = has_readdir;
    b->open = open;
    b->read = read;
    b->write = write;
    b->close = close;
    b->seek = seek;
    b->readdir = readdir;
    b->released = released;
    b->user = user;
    HASH_ADD_STR(computer->xfs_path_binds, path, b);

    pthread_mutex_unlock(&computer->run_mutex);
    return 1;
}

struct computer_xfs_path_bind_t* computer_find_xfs_path_bind(struct computer_t* computer, const char* path)
{
    struct computer_xfs_path_bind_t* b = NULL;
    HASH_FIND_STR(computer->xfs_path_binds, path, b);
    return b;
}

uint8_t computer_xfs_path_has_children(struct computer_t* computer, const char* path)
{
    struct computer_xfs_path_bind_t* b;
    size_t path_len = strlen(path);

    for (b = computer->xfs_path_binds; b; b = b->hh.next)
    {
        if (strcmp(path, "/") == 0)
        {
            if (strcmp(b->path, "/") != 0)
                return 1;
        }
        else if (strncmp(b->path, path, path_len) == 0 && b->path[path_len] == '/')
        {
            return 1;
        }
    }

    return 0;
}

uint8_t computer_write_port(struct computer_t* computer, uint16_t address, uint8_t value)
{
    int a = address;
    struct computer_port_write_binds_t* b = NULL;
    HASH_FIND_INT(computer->write_binds, &a, b);
    if (b == NULL)
        return 0;
    b->callback(b->user, value);
    return 1;
}

uint8_t computer_read_port(struct computer_t* computer, uint16_t address, uint8_t* result)
{
    int a = address;
    struct computer_port_read_binds_t* b = NULL;
    HASH_FIND_INT(computer->read_binds, &a, b);
    if (b == NULL)
        return 0;
    *result = b->callback(b->user);
    return 1;
}

uint8_t computer_write_bound_memory(struct computer_t* computer, uint16_t address, uint8_t value)
{
    struct computer_memory_write_binds_t* b;
    DL_FOREACH(computer->memory_write_binds, b)
    {
        if (address >= b->address && address < b->address + b->size)
        {
            b->callback(b->user, address - b->address, value);
            return 1;
        }
    }
    return 0;
}

uint8_t computer_read_bound_memory(struct computer_t* computer, uint16_t address, uint8_t* result)
{
    struct computer_memory_read_binds_t* b;
    DL_FOREACH(computer->memory_read_binds, b)
    {
        if (address >= b->address && address < b->address + b->size)
        {
            *result = b->callback(b->user, address - b->address);
            return 1;
        }
    }
    return 0;
}

static void VideoAddressToXY(uint16_t address, uint16_t* x, uint16_t* y)
{
    uint16_t x0_x4 = address & 0b11111;
    // ignored (as we divide by 8)
    // uint16_t y0_y2 = (address & 0b11100000000) >> 8;
    uint16_t y3_y5 = (address & 0b11100000) >> 5;
    uint16_t y6_y7 = (address & 0b1100000000000) >> 11;

    *x = x0_x4;
    *y = (y3_y5) | (y6_y7 << 3);
}

static uint16_t XYToVideoAddress(uint16_t x_, uint16_t y_)
{
    uint16_t x1 = x_ & 0b11111;
    y_ *= 8;
    uint16_t y1 = (y_ & 0b111) << 8;
    uint16_t y2 = (y_ & 0b111000) << 2;
    uint16_t y3 = (y_ & 0b11000000) << 5;
    uint16_t address = x1 | y1 | y2 | y3;

    return 0x4000 | address;
}

static uint8_t* encode_image_row(struct computer_t* computer, uint8_t* out_ptr, uint16_t x, uint16_t y, uint8_t len)
{
    // 3 byte header: for pixels
    // 1 byte length
    // 2 bytes memory location

    *out_ptr++ = len;
    uint16_t address = XYToVideoAddress(x, y);
    *out_ptr++ = address & 0xff;
    *out_ptr++ = (address >> 8) & 0xff;

    // pixels
    for (char z = 0; z < 8; z++)
    {
        memcpy(out_ptr, &computer->state.memory[address], len);
        out_ptr += len;
        address += 256;
    }

    // 3 byte header: for color
    // 1 byte length
    // 2 bytes memory location

    *out_ptr++ = len | 0x80;
    address = 0x5800 + y * 32 + x;
    *out_ptr++ = address & 0xff;
    *out_ptr++ = (address >> 8) & 0xff;

    memcpy(out_ptr, &computer->state.memory[address], len);
    out_ptr += len;
    return out_ptr;
}

static uint8_t computer_get_video_diff(struct computer_t* computer)
{
    if (computer->modified_screen_flag == 0)
    {
        return 0;
    }

    computer->modified_screen_flag = 0;

    uint16_t count = 0;
    for (int i = 0; i < sizeof(computer->modified_screen); i++)
    {
        if (computer->modified_screen[i])
        {
            count++;
        }
    }

    if (count >= 256)
    {
        memset(computer->modified_screen, 0, 768);

        server_push_memory(
            computer->active_session->state, computer->active_session,
            0x4000, &computer->state.memory[0x4000], 6912);
        return 1;
    }

    uint8_t* out_ptr = computer->int_buffer;

    uint8_t* modified_ptr = computer->modified_screen;
    for (int y = 0; y < 24; y++)
    {
        uint8_t sequence = 0;
        uint8_t sequence_from = 0;

        for (uint8_t x = 0; x < 32; x++)
        {
            uint8_t modified = *modified_ptr++;
            if (modified)
            {
                if (sequence == 0)
                {
                    sequence = 1;
                    sequence_from = x;
                }
            }
            else
            {
                if (sequence)
                {
                    // sequence_from .. x-1
                    uint16_t len = x - sequence_from;
                    out_ptr = encode_image_row(computer, out_ptr, sequence_from, y, len);
                    sequence = 0;
                }
            }
        }

        if (sequence)
        {
            // sequence_from .. x-1
            uint16_t len = 32 - sequence_from;
            out_ptr = encode_image_row(computer, out_ptr, sequence_from, y, len);
        }

        uint16_t len = out_ptr - computer->int_buffer;
        if (len >= 1000)
        {
            server_push_memory_diff(computer->active_session->state,
                computer->active_session, computer->int_buffer, len);

            out_ptr = computer->int_buffer;
        }
    }

    uint16_t len = out_ptr - computer->int_buffer;
    if (len)
    {
        server_push_memory_diff(computer->active_session->state,
            computer->active_session, computer->int_buffer, len);
    }

    return 0;
}

uint8_t computer_rom_load(struct computer_rom_t* rom, const char* filename)
{
    FILE* fp = fopen(filename, "rb");
    if (fp == NULL)
        return 1;

    fseek(fp, 0L, SEEK_END);
    rom->size = ftell(fp);
    rewind(fp);
    rom->rom = malloc(rom->size);
    fread(rom->rom, rom->size, 1, fp);
    fclose(fp);
    return 0;
}

struct __attribute__((packed)) sna_header_t
{
    uint8_t i;
    uint16_t hl_;
    uint16_t de_;
    uint16_t bc_;
    uint16_t af_;
    uint16_t hl;
    uint16_t de;
    uint16_t bc;
    uint16_t iy;
    uint16_t ix;
    uint8_t iff2;
    uint8_t r;
    uint16_t af;
    uint16_t sp;
    uint8_t im;
    uint8_t border;
};

uint8_t computer_snapshot_load(struct computer_t* computer, const uint8_t* data, uint32_t size)
{
    if (size < 49179)
    {
        return 1;
    }

    pthread_mutex_lock(&computer->run_mutex);

    struct sna_header_t* sna_header = (struct sna_header_t*)data;
    data += sizeof(struct sna_header_t);

    memcpy(&computer->state.memory[MEM_PAGE_RAM * MEM_PAGE_SIZE], data, 48 * 1024);
    computer->cpu.i = sna_header->i;
    computer->cpu.hl_.uint16_value = sna_header->hl_;
    computer->cpu.de_.uint16_value = sna_header->de_;
    computer->cpu.bc_.uint16_value = sna_header->bc_;
    computer->cpu.af_.uint16_value = sna_header->af_;
    computer->cpu.hl.uint16_value = sna_header->hl;
    computer->cpu.de.uint16_value = sna_header->de;
    computer->cpu.bc.uint16_value = sna_header->bc;
    computer->cpu.ix_iy[0].uint16_value = sna_header->ix;
    computer->cpu.ix_iy[1].uint16_value = sna_header->iy;
    computer->cpu.iff2 = computer->cpu.iff1;
    computer->cpu.r = sna_header->r;
    computer->cpu.af.uint16_value = sna_header->af;
    computer->cpu.sp.uint16_value = sna_header->sp;
    computer->cpu.im = sna_header->im;
    computer->state.ula_write = sna_header->border;

    z80_do_retn(&computer->cpu);

    computer->ula_write_dirty = 1;
    computer->modified_screen_flag = 1;
    memset(computer->modified_screen, 1, sizeof(computer->modified_screen));

    pthread_mutex_unlock(&computer->run_mutex);
    server_python_computer_notify_event(computer, "Snapshot has been loaded.");
    return 0;
}

void computer_read_memory(struct computer_t* computer, uint32_t address, uint8_t* data, uint16_t size)
{
    pthread_mutex_lock(&computer->run_mutex);
    memcpy(data, &computer->state.memory[address], size);
    pthread_mutex_unlock(&computer->run_mutex);
}

void computer_write_memory(struct computer_t* computer, uint32_t address, const uint8_t* data, uint16_t size)
{
    pthread_mutex_lock(&computer->run_mutex);
    memcpy(&computer->state.memory[address], data, size);
    pthread_mutex_unlock(&computer->run_mutex);
}

uint8_t computer_session_join(struct computer_t* computer, struct client_state_t* session)
{
    pthread_mutex_lock(&computer->run_mutex);
    if (computer->active_session && computer->active_session != session)
    {
        pthread_mutex_unlock(&computer->run_mutex);
        return 0;
    }

    if (computer->num_sessions == 0)
    {
        computer->first_session = 1;
    }
    else
    {
        computer->first_session = 0;
    }

    computer->num_sessions++;
    computer->active_session = session;

    pthread_mutex_unlock(&computer->run_mutex);

    char d[255];
    sprintf(d, "%s is watching screen", session->user_name);
    server_python_computer_notify_event(computer, d);

    return 1;
}

static void computer_keyboard_queue_free(struct computer_t* computer)
{
    struct keyboard_queue_t* tmp, *el;
    DL_FOREACH_SAFE(computer->keyboard_queue, el, tmp)
    {
        DL_DELETE(computer->keyboard_queue, el);
        free(el);
    }
}

uint8_t computer_session_leave(struct computer_t* computer)
{
    pthread_mutex_lock(&computer->run_mutex);

    if (computer->active_session == NULL)
    {
        pthread_mutex_unlock(&computer->run_mutex);
        return 0;
    }

    {
        char d[255];
        sprintf(d, "%s no longer watches screen", computer->active_session->user_name);
        server_python_computer_notify_event(computer, d);
    }

    computer->active_session = NULL;
    computer_keyboard_queue_free(computer);

    // reset the keyboard
    memset(computer->state.ula_read, 0b11111, sizeof(computer->state.ula_read));

    pthread_mutex_unlock(&computer->run_mutex);
    return 1;
}

void computer_session_key_action(struct computer_t* computer, uint8_t row, uint8_t key)
{
    pthread_mutex_lock(&computer->run_mutex);

    struct keyboard_queue_t* last = calloc(1, sizeof(struct keyboard_queue_t));
    last->row = row;
    last->key = key;

    DL_APPEND(computer->keyboard_queue, last);

    pthread_mutex_unlock(&computer->run_mutex);
}

static zuint8 z80_nmi_read(void *context, zuint16 address)
{
    struct computer_t* computer = context;
    computer->state.spectranet_nmi_flip_flop = 1;

    return 0;
}

static void z80_retn(void *context)
{
    struct computer_t* computer = context;
    computer->state.spectranet_nmi_flip_flop = 0;
}

static zuint8 z80_memory_fetch(void *context, zuint16 address)
{
    struct computer_t* computer = context;
    spectranet_check_pc_pre_fetch(computer, address);
    uint8_t res = z80_memory_read(context, address);
    spectranet_check_pc_post_fetch(computer);
    return res;
}

static zuint8 z80_memory_fetch_opcode(void *context, zuint16 address)
{
    struct computer_t* computer = context;

    if (computer->state.spectranet_trap && (computer->state.spectranet_trap_pc == address))
    {
        z80_nmi(&computer->cpu);
    }

    spectranet_check_pc_pre_fetch(computer, address);

    if (computer->state.spectranet_paged_in && (computer->cpu.pc.uint16_value == 0x007c)) {
        spectranet_page_out(computer);
    }

    uint8_t res = z80_memory_read(context, address);
    spectranet_check_pc_post_fetch(computer);
    return res;
}
