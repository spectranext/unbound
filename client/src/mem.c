#include "proto_req.h"
#include "client_graphics.h"
#include "client_map.h"
#include "particles.h"
#include "system.h"
#include <stdint.h>

struct proto_process_t process_proto;
struct proto_req_processor_t proto_req_processor = {};
struct client_map_effects_t map_effects = {};
struct map_client_object_t map_objects[MAX_CLIENT_CACHED_OBJECTS] = {};
uint8_t map_objects_last_known_size = 0;
struct particle_t particles[MAX_PARTICLES];

/*
 * MEMORY MAP
 *
 * 0x1000 - 0x1FFF - SPECTRANET PAGE A:
 * map objects, see get_objects
 * cached chunks, see memory_switch_cached_chunk
 * query results, see options_obtain_data
 * tile data, see switch_tile_data
 *
 * 0x2000 - 0x2FFF - SPECTRANET PAGE B:
 * common arrays, see below
 */

__at(0x2000) uint8_t proto_buffer_b[2048];
__at(0x2800) struct client_map_t client_map_b;

__at(0x1800) uint8_t screen_characters_a[1536];
__at(0x2800) uint8_t screen_characters_b[1536];

__at(0x1000) uint8_t tiles_a[];
__at(0x1800) uint8_t tiles_colors_a[];

uint16_t my_client_id = 0;
struct map_client_object_t* my_player_object = NULL;
uint16_t my_team_id = 0;
uint8_t panel = 0;
uint8_t camera_x = 0xFF;
uint8_t camera_y = 0xFF;
uint8_t camera_x_end = 0xFF;
uint8_t camera_y_end = 0xFF;
uint16_t camera_base_x = 0;
uint16_t camera_base_y = 0;
uint16_t camera_low_phy_x = 0;
uint16_t camera_low_phy_y = 0;
uint16_t camera_high_phy_x = 0;
uint16_t camera_high_phy_y = 0;

uint8_t animation = 0;
uint8_t animation_move = 0;
uint8_t animation_pick = 0;
uint8_t animation_tick = 0;
