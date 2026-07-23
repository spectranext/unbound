#ifndef NOTIFICATIONS_H
#define NOTIFICATIONS_H

#include <stdint.h>

enum notification_state_t {
    NOTIFICATION_STATE_NONE = 0,
    NOTIFICATION_STATE_FLASH,
    NOTIFICATION_STATE_SHOWING,
    NOTIFICATION_STATE_BLOCKED
};

extern enum notification_state_t notification_state;

extern void clear_notification_progress();
extern void show_notification_progress(uint8_t progress);
extern void show_notification(const char* text, uint8_t len, uint8_t color);
extern void update_notifications();

#endif