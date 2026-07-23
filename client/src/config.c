#include "config.h"

#define CONFIG_ACCESS_TOKEN 0x3F

void config_init() __naked
{
#ifndef __CLION_IDE__
#asm
    defc MODULECALL_NOPAGE = 0x28
    ld hl, 0xFE01
    rst MODULECALL_NOPAGE
    ret
#endasm
#endif
}

uint8_t config_find_section() __naked
{
#ifndef __CLION_IDE__
#asm
    ld de, 0x01FF
    ld hl, 0xFE02
    rst MODULECALL_NOPAGE

    ld hl, 0
    ret nc
    inc l
    ret
#endasm
#else
                return 1;
#endif
}

uint8_t config_create_section() __z88dk_fastcall
{
#ifndef __CLION_IDE__
#asm
    ld de, 0x01FF
    ld hl, 0xFE06
    rst MODULECALL_NOPAGE

    ld hl, 0
    ret nc
    inc l
    ret
#endasm
#else
    return 1;
#endif
}

uint8_t config_setCFString(const char* value) __naked __z88dk_fastcall
{
#ifndef __CLION_IDE__
#asm
    ld d, h
    ld e, l
    ld a, CONFIG_ACCESS_TOKEN
    ld hl, 0xFE0A
    rst MODULECALL_NOPAGE

    ld hl, 0
    ret nc
    inc l
    ret
#endasm
#else
    return 1;
#endif
}

uint8_t config_getCFString(char* value) __naked __z88dk_fastcall
{
#ifndef __CLION_IDE__
#asm
    ld d, h
    ld e, l
    ld a, CONFIG_ACCESS_TOKEN
    ld hl, 0xFE03
    rst MODULECALL_NOPAGE

    ld hl, 0
    ret nc
    inc l
    ret
#endasm
#else
    return 1;
#endif
}

uint8_t config_commit_config() __naked
{
#ifndef __CLION_IDE__
#asm
    ld hl, 0xFE07
    rst MODULECALL_NOPAGE

    ld hl, 0
    ret nc
    inc l
    ret
#endasm
#else
return 1;
#endif
}
