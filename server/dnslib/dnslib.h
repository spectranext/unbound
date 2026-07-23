#ifndef LIBDNS_H
#define LIBDNS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>

extern uint8_t dns_parse(uint8_t* packet, uint32_t size, char* output, uint16_t* request_id);
extern uint8_t dns_encode(const char* name, uint16_t request_id, uint32_t ip4response, uint8_t* packet, uint32_t* outSize);

#ifdef __cplusplus
}
#endif

#endif