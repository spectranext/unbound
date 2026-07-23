#ifndef __STATE_H__
#define __STATE_H__

#include <stdint.h>

extern uint8_t state_active_phase;

enum control_mode_t
{
    CONTROL_MODE_MOVE = 0,
    CONTROL_MODE_TOUCH,
    CONTROL_MODE_TOUCH_SCHEDULE,
    CONTROL_MODE_SELECT_BLOCK,
    CONTROL_MODE_PANEL
};

extern enum control_mode_t control_mode;

extern uint8_t my_health;
extern uint8_t my_temperature;
extern uint8_t my_power;
extern uint16_t my_credits;
extern uint8_t my_hit_auto;
extern uint8_t my_hit_delay;
extern uint8_t my_stats_dirty;
extern uint8_t construction_mode_dirty;
extern char my_default_state[20];
extern char my_building_state[20];

extern void render_my_stats();
extern void clear_scheduled_touch();

extern void game_state_loop();

#endif