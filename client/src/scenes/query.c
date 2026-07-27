#include "system.h"
#include "scenes.h"
#include "client_map.h"
#include "messages.h"
#include "soundfx.h"
#include "proto.h"

void init_query()
{
}

void switch_query(const char* query) __z88dk_fastcall
{
    declare_str_property_on_stack(req_id, OBJ_PROPERTY_ID, MSG_QUERY, NULL);
    declare_str_property_on_stack(version, 'q', query, &req_id);
    declare_object_on_stack(request, 128, &version);

    proto_send_one_nf(request);
}
