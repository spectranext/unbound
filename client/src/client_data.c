#include <string.h>
#include "client_data.h"
#include "client_map.h"
#include "spectranet.h"

struct client_data_t data_entries[MAXIMUM_DATA_ENTRIES] = {};

static uint8_t current_page = SPECTRANET_DATA_PAGES;
static uint16_t current_offset = 0;
uint8_t registered_data_entries = 0;

uint8_t* switch_data_entry_a(uint8_t entry_id) __z88dk_fastcall
{
    struct client_data_t* de = &data_entries[entry_id & 0x7F];
    setpagea(de->spectranet_page);
    return de->memory_location;
}

uint8_t register_data_entry(const uint8_t* payload, uint16_t data_len, uint16_t payload_len)
{
    if (registered_data_entries >= MAXIMUM_DATA_ENTRIES)
        return 0xFF;

    if (current_offset + payload_len > 0x1000)
    {
        current_page++;
        current_offset = 0;
    }

    struct client_data_t* e = &data_entries[registered_data_entries];

    if (e->spectranet_page == 0)
    {
        e->spectranet_page = current_page;
        e->memory_location = (uint8_t*)(0x1000 + current_offset);
        current_offset += payload_len;
    }

    setpagea(e->spectranet_page);
    memcpy(e->memory_location, payload, data_len);
    return registered_data_entries++;
}