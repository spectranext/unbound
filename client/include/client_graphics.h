#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <stdint.h>
#include "env.h"

extern uint8_t tiles_a[];
extern uint8_t tiles_colors_a[];

extern uint8_t rendering_blocked;
extern uint8_t skip_rendering_while_dirty;

extern void switch_sprite_data_a();
extern void switch_sprite_data_b();
extern void switch_tile_data_a();

extern void render_sprite(const uint8_t* sprite_data, uint16_t xy_offset, uint16_t xy) __z88dk_callee;
extern void render_sprite_pre_shifted(const uint8_t* sprite_data, uint16_t xy_offset, uint16_t xy) __z88dk_callee;

extern void render_tile_icon(uint8_t x, uint8_t y, uint8_t icon) __z88dk_callee;
extern void redraw_tile(uint16_t old_new, uint16_t xy) __z88dk_callee;
extern void render_line(uint16_t xy1, uint16_t xy2) __z88dk_callee;
extern void render_x(uint16_t xy) __z88dk_callee;

extern void render_screen();
extern void render_chunk(uint16_t xy) __z88dk_fastcall;

extern uint8_t isr_render_enabled;

#endif
