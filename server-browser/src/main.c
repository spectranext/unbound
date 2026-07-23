#pragma clang diagnostic push
#pragma ide diagnostic ignored "EndlessLoop"

#include "zxgui.h"
#include <spectrum.h>
#include <intrinsic.h>
#include <im2.h>
#include "spectranet.h"
#include <stdlib.h>
#include <string.h>
#include "scenes.h"
#include "loader.h"

extern uint8_t intro[6912];

#ifndef __CLION_IDE__
#asm
public _game_loader_bin
public _game_loader_bin_end

_game_loader_bin:
incbin "../game_loader.bin"
_game_loader_bin_end:

#endasm
#endif

extern uint8_t game_loader_bin[];
extern uint8_t game_loader_bin_end[];

static void starts(void)
{
    static const uint8_t star_positions[][2] = {
        {6, 7},
        {26, 9},
        {30, 11},
        {22, 12}
    };
    static uint8_t frame_counter = 0;
    uint8_t star_index;
    uint8_t star_x;
    uint8_t star_y;
    uint8_t *attribute_address;

    ++frame_counter;
    if (frame_counter < 100)
    {
        return;
    }

    frame_counter = 0;
    star_index = rand() & 0x03;
    star_x = star_positions[star_index][0];
    star_y = star_positions[star_index][1];
    attribute_address = (uint8_t *)0x5800 + (star_y * 32) + star_x;

    *attribute_address = (rand() & 0x01) ? (INK_YELLOW | BRIGHT) : INK_BLACK;
}

void switch_main()
{
    zxgui_clear();
    memcpy((void*)0x4000, intro, sizeof(intro));
    switch_auth();
}

int main()
{
    pagein();
    setpagea(SPECTRANET_LOADER_PAGE0);
    setpageb(SPECTRANET_LOADER_PAGE1);
    memcpy((void*)0x1000, (void*)game_loader_bin, game_loader_bin_end - game_loader_bin);

    switch_main();

    while (1)
    {
        starts();
        zxgui_scene_iteration();
    }
}
#pragma clang diagnostic pop
