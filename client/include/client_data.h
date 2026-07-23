#ifndef __CLIENT_DATA_H__
#define __CLIENT_DATA_H__

#include <stdint.h>

#define MAXIMUM_DATA_ENTRIES (64)

struct client_data_t
{
    uint8_t spectranet_page;
    uint8_t* memory_location;
};

extern struct client_data_t data_entries[MAXIMUM_DATA_ENTRIES];
extern uint8_t registered_data_entries;

extern void reset_data_entry_a();
extern uint8_t* switch_data_entry_a(uint8_t entry_id) __z88dk_fastcall;
extern uint8_t register_data_entry(const uint8_t* payload, uint16_t data_len, uint16_t payload_len);

#endif