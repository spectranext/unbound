#ifndef __NET_H__
#define __NET_H__

#include <proto_objects.h>

void client_new_message(void* user);
extern void client_message_object(ProtoObject* object, void* user);
extern const char* client_message_complete(void* user);

#endif