#pragma clang diagnostic push
#pragma ide diagnostic ignored "EndlessLoop"

#include "system.h"
#include "client.h"
#include "state.h"
#include "zxgui.h"
#include "proto_req.h"
#include <intrinsic.h>
#include <im2.h>
#include "scenes.h"
#include "key_controls.h"
#include "notifications.h"
#include "client_graphics.h"
#include "modules.h"
#include "vt_sound.h"
#include "particles.h"
#include "hud.h"
#include "launcher_api.h"
#include "printf.h"
#include "config.h"

int heap = 0;
uint8_t unique_frame = 0;
uint8_t module_loop_active = 0xFF;
uint8_t module_interrupt_active = 0xFF;
uint8_t module_music_active = 0;
uint8_t current_scene_module = MODULE_NONE;
uint8_t module_call_namespace = 0;
uint8_t module_loaded[MODULE_NAMESPACE_COUNT] = {};

extern void isr_handler();

void module_scene_set(struct gui_scene_t* scene) __z88dk_fastcall
{
    current_scene_module = module_call_namespace;
    zxgui_scene_set(scene);
}

void module_scene_clear()
{
    current_scene_module = MODULE_NONE;
    zxgui_scene_set(NULL);
}

static void install_isr()
{
    intrinsic_di();

#ifndef __CLION_IDE__
#asm
    ld a, 0xFE
    ld i, a
    im 2
#endasm
#endif

    memset((void*)0xFE00, 0xFD, 257);
    *(uint8_t*)0xFDFD = 0xC3;
    *(uint16_t*)0xFDFE = (uint16_t)isr_handler;
    intrinsic_ei();
}

char server_connect_addr[64] = {"127.0.0.1"};
uint16_t server_connect_port = 13390;

int main()
{
    pagein();

    if (launcher_magic == 1337)
    {
        strcpy(server_connect_addr, launcher_server_addr);
        server_connect_port = launcher_server_port;
    }

    install_isr();

    config_init();
    client_map_get_b();

    zxgui_clear();

    // scenes
    init_terminal();
    init_query();

    init_particles();
    init_hud();
    my_default_state[0] = 0;

    client_map_init();
    proto_init(proto_buffer_b, sizeof(proto_buffer_b));

    print("Connecting...\n");

    do_connect();

    while (1)
    {
        game_state_loop();
    }
}
#pragma clang diagnostic pop
