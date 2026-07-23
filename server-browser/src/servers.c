
#include "server.h"

#ifdef SERVERS_DEBUG
#include <spectrum.h>
struct server_t servers[MAX_SERVERS] = {
    {.title = "Unbound | Winter | Players: 10", .address = "unbound.desertkun.in", .port = 13390,
     .icon = {0x0f, 0x1e, 0x3c, 0x78, 0xf0, 0xe1, 0xc3, 0x87}, .icon_color = INK_GREEN | BRIGHT},
    {.title = "Mac World | 13390 | Players: 0", .address = "127.0.0.1", .port = 13390,
     .icon = {0x00, 0x6c, 0xfe, 0xfe, 0xfe, 0x7c, 0x38, 0x10}, .icon_color = INK_RED | BRIGHT},
    {.title = "Mac World | 13391 | Players: 0", .address = "127.0.0.1", .port = 13391,
     .icon = {0x00, 0x6c, 0xfe, 0xfe, 0xfe, 0x7c, 0x38, 0x10}, .icon_color = INK_RED | BRIGHT}
};
#else
__at(25000) struct server_t servers[MAX_SERVERS];
#endif