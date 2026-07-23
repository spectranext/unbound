#ifndef __CLIENT_H__
#define __CLIENT_H__

#include <proto.h>
#include "client_map.h"

extern uint16_t my_client_id;
extern struct map_client_object_t* my_player_object;
extern uint16_t my_team_id;
extern uint8_t camera_x;
extern uint8_t camera_y;
extern uint8_t camera_x_end;
extern uint8_t camera_y_end;
extern uint16_t camera_base_x;
extern uint16_t camera_base_y;
extern uint16_t camera_low_phy_x;
extern uint16_t camera_low_phy_y;
extern uint16_t camera_high_phy_x;
extern uint16_t camera_high_phy_y;
extern uint8_t panel;

extern struct proto_process_t process_proto;
extern struct proto_req_processor_t proto_req_processor;
extern struct client_map_t client_map_b;
extern struct client_map_effects_t map_effects;
extern struct map_client_object_t map_objects[MAX_CLIENT_CACHED_OBJECTS];
extern uint8_t map_objects_last_known_size;

extern uint8_t proto_buffer_b[2048];

extern void get_objects_a();

extern void panel_open();
extern void panel_close();
extern void update_camera_bounds();

extern void do_connect();
extern void restart_to_main();


extern void client_action(const char* action);
extern void client_connect(const char* address, uint16_t port);

#endif
