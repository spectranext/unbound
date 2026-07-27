#ifndef SCENES_H
#define SCENES_H

#include <proto.h>
#include <proto_objects.h>

extern void init_chat();
extern void init_terminal();
extern void init_query();
extern void char_msg(const char* msg, uint16_t len) __z88dk_fastcall;
extern void switch_alert(const char* progress_message);

extern void switch_chat();
extern void switch_terminal();
extern void switch_query(const char* query) __z88dk_fastcall;

#endif
