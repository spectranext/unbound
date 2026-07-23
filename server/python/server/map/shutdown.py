from .. api.map import MapAPI

def shutdown_map(server_map: MapAPI):
    server_map.on_shutdown()
