#ifndef LAUNCHER_API
#define LAUNCHER_API

#include <stdint.h>

__at(0x2FBB) uint16_t launcher_magic;
__at(0x2FBE) uint16_t launcher_server_port;
__at(0x2FC0) char launcher_server_addr[64];

#endif