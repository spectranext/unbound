#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdint.h>
#include <stdlib.h>

#include <input.h>

#include <arch/zx/spectrum.h>

#define int_to_string(i, s) itoa(i, s, 10)

#define SCREEN_WIDTH (32)
#define SCREEN_HEIGHT (24)
#define CHARACTERS_PER_CELL (2)
#define BLINK_INTERVAL      (10)

#define set_border_color    zx_border

extern void render_blink_even(uint8_t *addr) __z88dk_fastcall __naked;
extern void render_blink_odd(uint8_t *addr) __z88dk_fastcall __naked;
extern void clear_blink(struct gui_edit_t* this) __z88dk_fastcall;

#endif