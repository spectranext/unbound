#include "key_controls.h"

uint8_t poll_key_row(uint row) __naked __z88dk_fastcall
{
#asm
    ld bc, hl
    in a, (c)
    ld h, 0
    ld l, a
    ret
#endasm
}
