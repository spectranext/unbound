from .. api.map import MapAPI


def debug_map(server_map: MapAPI):
    map_width = server_map.get_width()
    map_height = server_map.get_height()

    for y in range(0, map_height):
        for x in range(0, map_width):
            print("*" if server_map.get_block(x, y).blocking() else " ", end="")
        print()
