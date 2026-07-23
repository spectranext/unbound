#ifndef SOUND_FX
#define SOUND_FX

#include <stdint.h>

enum fx_t
{
    FX_SHOT_2 = 0,
    FX_PICK,
    FX_DAMAGE,
    FX_ITEM_2,
    FX_ITEM_3,
    FX_ITEM_4,
    FX_ITEM_6,
    FX_BEEP,
    FX_PICKAXE,
    FX_SHOTGUN,
    FX_SHOT_3,
    FX_,
};

extern void soundfx(enum fx_t sound) __z88dk_fastcall;

#endif