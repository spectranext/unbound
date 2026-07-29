#ifndef MODULES_H
#define MODULES_H

#include "env.h"
#include "proto_objects.h"
#include "zxgui.h"

extern uint8_t module_loop_active;
extern uint8_t module_interrupt_active;
extern uint8_t module_music_active;
extern uint8_t current_scene_module;
extern uint8_t module_call_namespace;
extern uint8_t module_loaded[];

#define MODULE_NONE 0xFF
#define MODULE_NAMESPACE_COUNT 4

extern void module_action(ProtoObject* proto_object) __z88dk_fastcall;
extern void module_loop();
extern void module_interrupt();
extern void module_scene_set(struct gui_scene_t* scene) __z88dk_fastcall;
extern void module_scene_clear();

#endif
