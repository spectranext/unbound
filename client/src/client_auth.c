#include "state.h"
#include "scenes.h"
#include "spectranet.h"
#include "client.h"
#include "messages.h"
#include "proto_req.h"
#include "client_data.h"
#include "client_graphics.h"
#include "printf.h"
#include "config.h"

uint8_t last_data_value = 0;

void client_auth_object_callback(uint8_t index, ProtoObject* object)
{
    if (index == 0)
    {
        client_map_b.map.width = get_uint16_property(object, 'w', 0);
        client_map_b.map.height = get_uint16_property(object, 'h', 0);

        camera_x = get_uint8_property(object, 'x', 0xFF);
        camera_y = get_uint8_property(object, 'y', 0xFF);

        if (camera_x != 0xFF)
        {
            update_camera_bounds();
            client_map_b.screen_dirty = 1;
        }

        my_client_id = get_uint16_property(object, 'i', 0);

        print("my client id ");
        printn(my_client_id);
        print("\n");

        data_entries[0].spectranet_page = SPECTRANET_TILES_PAGE;
        data_entries[0].memory_location = tiles_a;

        ProtoObjectProperty* new_token = find_property(object, 't');
        if (new_token && new_token->value_size)
        {
            /*
            char token_data[68];
            memcpy(token_data, new_token->value, new_token->value_size);
            token_data[new_token->value_size] = 0;

            if (config_find_section())
            {
                config_create_section();
            }

            config_setCFString(token_data);
            config_commit_config();

            switch_tile_data_a();
             */
        }

        registered_data_entries = 0;

        return;
    }

    uint16_t sz = get_uint16_property(object, 's', 0);
    ProtoObjectProperty* payload = find_property(object, 'p');

    if (payload)
    {
        if (sz)
        {
            last_data_value = register_data_entry((const uint8_t*)payload->value, payload->value_size, sz);
        }
        else
        {
            uint16_t offset = get_uint16_property(object, 'o', 0);
            uint8_t* ptr = switch_data_entry_a(last_data_value);
            memcpy(ptr + offset, payload->value, payload->value_size);
        }
    }
}

void client_auth_complete_callback(struct proto_process_t* proto)
{
    rendering_blocked = 0;
    control_mode = CONTROL_MODE_MOVE;
    state_active_phase = 1;
}

void client_auth_error_callback(const char* error)
{
    switch_alert(error);
    proto_disconnect();
}

void client_auth()
{
    char auth_token[68] = {};
    /*
    char* auth_token_data = NULL;

    if (config_find_section() == 0)
    {
        if (config_getCFString(auth_token) == 0)
        {
            auth_token_data = auth_token;
        }
    }
     */

    switch_tile_data_a();

    declare_str_property_on_stack(req_id, OBJ_PROPERTY_ID, MSG_AUTH, NULL);
    declare_str_property_on_stack(c, 't', /* auth_token_data ? auth_token_data : */"", &req_id);
    declare_object_on_stack(request, 128, &c);

    proto_req_send_request(request, client_auth_object_callback,
        client_auth_complete_callback, client_auth_error_callback);
}

void client_action(const char* action)
{
    declare_str_property_on_stack(req_id, OBJ_PROPERTY_ID, MSG_ACTION, NULL);
    declare_str_property_on_stack(version, 'm', action, &req_id);
    declare_object_on_stack(request, 128, &version);

    proto_send_one_nf(request);
}
