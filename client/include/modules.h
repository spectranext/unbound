#ifndef MODULES_H
#define MODULES_H

#include "env.h"
#include "proto_objects.h"

extern uint8_t module_loop_active;
extern uint8_t module_interrupt_active;
extern uint8_t module_music_active;

extern void module_action(ProtoObject* proto_object) __z88dk_fastcall;
extern void module_loop();
extern void module_interrupt();

#endif