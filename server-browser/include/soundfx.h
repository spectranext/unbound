#ifndef SOUND_FX
#define SOUND_FX

#include <stdint.h>

enum fx_t
{
    FX_ITEM_2 = 0,
    FX_
};

extern void soundfx(enum fx_t sound) __z88dk_fastcall;

#endif