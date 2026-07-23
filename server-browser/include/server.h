#ifndef SERVER_H
#define SERVER_H

#include <stdint.h>

#ifdef SERVERS_DEBUG
#define MAX_SERVERS 4
#else
#define MAX_SERVERS 20
#endif

// 200 bytes each
struct server_t
{
    char title[64];
    char address[64];
    uint16_t port;
    char icon[8];
    char icon_color;
    char reserved[61];
};

extern struct server_t servers[MAX_SERVERS];

#endif