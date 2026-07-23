#ifndef HUD_H
#define HUD_H

#include <stdint.h>

extern uint8_t hud_dirty;

extern void init_hud();
extern void update_target_marker();
extern void enable_target_marker();
extern void disable_target_marker();
extern void show_hud();
extern void hide_hud();
extern void clear_hud();
extern uint16_t get_target_angle();
extern void target_marker_cw();
extern void target_marker_ccw();
extern void target_marker_stop();
extern void render_hud();

#endif