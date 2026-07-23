#ifndef LOADER_H
#define LOADER_H

#define SPECTRANET_LOADER_PAGE0 0xCA
#define SPECTRANET_LOADER_PAGE1 0xCB

extern int loader_load_server(const char* server, uint16_t port) __z88dk_callee;

#endif