#ifndef __CHAT_H__
#define __CHAT_H__

#include "zxgui.h"

#define MAX_MESSAGES (4)

struct chat_message_t {
    char msg[64];
    struct gui_label_t label;
};

static uint8_t msg_index = 0;

#endif