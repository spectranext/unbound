#include "client_graphics.h"
#include "client_map.h"
#include "spectranet.h"
#include "client.h"
#include <spectrum.h>

uint16_t sprites_allocated = 0;
uint8_t rendering_blocked = 0;
uint8_t skip_rendering_while_dirty = 0;
uint8_t sprite_tiles_dirty_num = 0;
uint8_t isr_render_enabled = 1;

void switch_tile_data_a()
{
#ifndef __CLION_IDE__
#asm
    extern SETPAGEA
    ld a, SPECTRANET_TILES_PAGE
    jp SETPAGEA
#endasm
#endif
}

void switch_sprite_data_a() __naked
{
#ifndef __CLION_IDE__
#asm
    extern SETPAGEA
    ld a, SPECTRANET_SPRITES_PAGE
    jp SETPAGEA
#endasm
#endif
}

void switch_sprite_data_b()
{
#ifndef __CLION_IDE__
#asm
    extern SETPAGEB
    ld a, SPECTRANET_SPRITES_PAGE
    jp SETPAGEB
#endasm
#endif
}

void render_tile_icon(uint8_t x, uint8_t y, uint8_t icon) __z88dk_callee
{
#ifndef __CLION_IDE__
#asm
    extern SETPAGEA
    ld a, SPECTRANET_TILES_PAGE
    call SETPAGEA
#endasm
#endif

    uint8_t* data = tiles_a + icon;
    uint8_t* addr = zx_cxy2saddr(x, y);

    for (uint8_t i = 0; i < 8; i++)
    {
        *addr = *data;
        data += 256;
        addr += 256;
    }

    *zx_cxy2aaddr(x, y) = tiles_a[(uint16_t)icon + 2048];

#ifndef __CLION_IDE__
#asm
    extern SETPAGEA
    ld a, SPECTRANET_MODULES_NAMESPACE0
    call SETPAGEA
#endasm
#endif
}
