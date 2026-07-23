from typing import Dict
from . import MapAPI


class Superstructure(object):
    SUPERSTRUCTURES: Dict[str, 'Superstructure'] = {}

    def __init__(self, name: str, data: list, width: int, height: int):
        self.name = name
        self.data = data
        self.width = width
        self.height = height

    def apply(self, server_map: MapAPI, x: int, y: int, check_empty: bool, offset_top: bool, *args, **kwargs) -> bool:
        if offset_top:
            y -= self.height
        server_map.print("placing {0} at {1}x{2}".format(self.name, x, y))
        if check_empty:
            for y_ in range(0, self.height):
                for x_ in range(0, self.width):
                    b = server_map.get_block(x + x_, y + y_)
                    if b and b.code:
                        return False
        for y_ in range(0, self.height):
            for x_ in range(0, self.width):
                b = self.data[y_][x_]
                if not b:
                    continue
                a = b(*args, **kwargs)
                if not a:
                    continue
                server_map.set_block(x + x_, y + y_, a, False)
        return True
