
from .. api.map import MapAPI
from .. api.query import QueryResponse, QueryResponseOption, OPT, NOACT
from . import PlayerObject
from .. art import MAP
from .. imagegen import Image
from .. blocks import MAXIMUM_LIGHT, BLOCKING


class MapQueryResponse(QueryResponse):
    def __init__(self, p: PlayerObject, zoom: int = 1):
        super().__init__(b"", b"Map")
        self.p = p

        if p.health > 0:
            im = Image(source=MAP)

            m = MapAPI.instance
            map_width = m.get_width()
            map_height = m.get_height()
            range_width = 128
            range_height = 80
            range_x = 8
            range_y = 8

            player_x = int(p.get_x())
            player_y = int(p.get_y())

            player_offset_x = range_width // 2
            player_offset_y = range_height // 2

            def test(xx, yy) -> bool:
                xx += player_x - player_offset_x
                yy += player_y - player_offset_y
                if xx < 0 or yy < 0:
                    return False
                if xx > map_width or yy > map_height:
                    return False
                bb = m.get_block(xx, yy)
                if bb is None:
                    return False
                if bb.code & BLOCKING == 0:
                    return False
                light = bb.get_light()
                if light == 0:
                    return False
                if light != MAXIMUM_LIGHT:
                    hor = xx % 2 == 0
                    ver = yy % 2 == 0
                    # chess patter for low light
                    return hor == ver
                return True

            for y in range(0, range_height):
                for x in range(0, range_width):
                    res = test(x, y)
                    if not res:
                        im.set_pixel(x + range_x, y + range_y, 0)

            self.image = im.bake()
        self.actions = [b"OK"]
