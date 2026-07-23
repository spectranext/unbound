
#ifndef __CLION_IDE__
#asm
org 4096
section CODE_0
#endasm
#endif

#include <spectrum.h>
#include <stdint.h>
#include <string.h>
#include "proto.h"
#include "proto_req.h"

extern uint8_t progress_bar_bg[512];
uint8_t last_full_progress = 0;
uint16_t execute_address = 0;
uint16_t download_address = 0;
uint16_t total_downloaded = 0;
uint16_t execute_size = 0;
uint16_t remaining_size = 0;
uint16_t requested_size = 0;
uint8_t downloading_complete = 0;

// max progress = 30 * 8 = 240
static void show_progress(uint8_t progress) __z88dk_fastcall
{
    uint8_t new_full_progress = progress >> 3;

    for (uint8_t i = last_full_progress; i < new_full_progress; i++)
    {
        uint8_t x = 8 + (i << 3);

        uint8_t* c = zx_pxy2saddr(x, 95);
        *c = 0xFF;
        c = zx_pxy2saddr(x, 96);
        *c = 0xFF;
    }

    last_full_progress = new_full_progress;

    uint8_t remaining_progress = progress % 8;
    if (remaining_progress)
    {
        uint8_t need = (uint16_t)0xFF00 >> remaining_progress;

        uint8_t x = 8 + (last_full_progress << 3);
        uint8_t* c = zx_pxy2saddr(x, 95);
        *c = need;
        c = zx_pxy2saddr(x, 96);
        *c = need;
    }
}

#ifndef __CLION_IDE__
#asm
public _progress_bar_bg
_progress_bar_bg:
    incbin "../progress_bar.bin"
#endasm
#endif


uint8_t proto_buffer[1200];
struct proto_process_t process_proto = {};
struct proto_req_processor_t proto_req_processor = {};

static void render_ui()
{
    memset((void*)0x4000, 0, 6144);
    memset((void*)0x5800, INK_GREEN | PAPER_BLACK | BRIGHT, 768);

    uint8_t* ptr = progress_bar_bg;
    uint8_t* ptr_end = ptr + sizeof(progress_bar_bg);

    for (uint8_t row = 88; ptr < ptr_end; row++, ptr += 32)
    {
        memcpy(zx_pxy2saddr(0, row), ptr, 32);
    }
}

static void disconnected()
{
}

static void download_next_chunk();

void download_binary_object(uint8_t index, ProtoObject* object)
{
    uint16_t address = get_uint16_property(object, 'a', 0);
    ProtoObjectProperty* data = find_property(object, 'd');
    if (data)
    {
        memcpy((void*)address, data->value, data->value_size);
    }
}

void download_binary_complete(struct proto_process_t* proto)
{
    download_address += requested_size;
    remaining_size -= requested_size;
    total_downloaded += requested_size;

    uint32_t progress = (((uint32_t)total_downloaded) * 240) / execute_size;
    show_progress(progress);

    if (remaining_size == 0)
    {
        downloading_complete = 1;
    }
    else
    {
        download_next_chunk();
    }
}

void download_binary_error(const char* error)
{
}

static void download_next_chunk()
{
    requested_size = 1000;
    if (remaining_size < requested_size)
    {
        requested_size = remaining_size;
    }
    declare_str_property_on_stack(req_id, OBJ_PROPERTY_ID, "dw", NULL);
    declare_arg_property_on_stack(_addr, 'a', download_address, &req_id);
    declare_arg_property_on_stack(_size, 's', requested_size, &_addr);
    declare_object_on_stack(request, 32, &_size);

    proto_req_send_request(request, download_binary_object,
           download_binary_complete, download_binary_error);
}

void get_client_binary_object(uint8_t index, ProtoObject* object)
{
    execute_address = get_uint16_property(object, 'a', 0);
    execute_size = get_uint16_property(object, 's', 0);
}

void get_client_binary_complete(struct proto_process_t* proto)
{
    download_address = execute_address;
    total_downloaded = 0;
    remaining_size = execute_size;
    download_next_chunk();
}

void get_client_binary_error(const char* error)
{
}

static void execute() __naked
{
    // jump to it
#ifndef __CLION_IDE__
#asm
    ld hl, (_execute_address)
    jp (hl)
#endasm
#endif
}

int loader_load_server(const char* server, uint16_t port) __z88dk_callee
{
    render_ui();

    proto_init(proto_buffer, sizeof(proto_buffer));
    proto_req_init_processor(NULL, NULL, NULL, NULL);

    int err = proto_connect(server, (int)port, disconnected);
    if (err < 0)
        return err;

    {
        declare_str_property_on_stack(req_id, OBJ_PROPERTY_ID, "bi", NULL);
        declare_object_on_stack(request, 32, &req_id);

        proto_req_send_request(request, get_client_binary_object,
           get_client_binary_complete, get_client_binary_error);
    }

    while(1)
    {
        proto_client_process(proto_req_new_request, proto_req_object_callback, proto_req_recv, &proto_req_processor);

        if (downloading_complete && execute_address)
        {
            break;
        }
    }

    proto_disconnect();
    execute();

    return 0;
}