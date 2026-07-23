from typing import Set, Dict

from . api.map import MapAPI
from . blockspawn import BlockSpawn
from . api.block import NeighboringBlockObject
from . neighbors import NeighboringSet
from . import items
from . import blocks


class Liquids(object):
    def __init__(self):
        self.objects: Set['LiquidBlockObject'] = set()

    def register(self, o: 'LiquidBlockObject'):
        self.objects.add(o)

    def unregister(self, o: 'LiquidBlockObject'):
        self.objects.remove(o)

    def update(self):
        for o in self.objects.copy():
            o.update()


class LiquidBlockObject(NeighboringBlockObject):
    def __init__(self, neighboring_set: NeighboringSet, is_blocking: bool = False,
                 dark_top: int = 0, dark_middle: int = 0,
                 proto: Dict[bytes, bytes] = None, **kwargs):
        super().__init__(neighboring_set, is_blocking=is_blocking, proto=proto, **kwargs)
        self.dark_top = dark_top
        self.dark_middle = dark_middle
        MapAPI.instance.liquids.register(self)

    def release(self, notify: bool):
        MapAPI.instance.liquids.unregister(self)
        super().release(notify)

    def get_good_light_code(self, light: int):
        return super().get_some_light_code(light)

    def get_midrange_light_code(self, light: int):
        return super().get_some_light_code(light)

    def get_some_light_code(self, light: int):
        if self.neighbor_code & 1 == 0:
            return self.dark_top
        return self.dark_middle

    def propagate(self, x: int, y: int) -> bool:
        x += self.x
        y += self.y
        if (y < 0) or (y >= MapAPI.instance.get_height()):
            return False
        if (x < 0) or (x >= MapAPI.instance.get_width()):
            return False
        b = MapAPI.instance.get_block(x, y)
        if b is None:
            return False
        if b.item == self.item:
            return False
        if b.blocking():
            return False
        if not b.flushable():
            return False
        MapAPI.instance.set_block(x, y, BlockSpawn.create_block(self.item), True)
        return True

    def flushable(self):
        return False

    def pass_light(self, light: int) -> int:
        light -= 1
        return light if light > 0 else 0

    def update(self):
        self.refresh()

        a = self.propagate(0, 1)
        b = self.propagate(1, 0)
        c = self.propagate(-1, 0)

        if a or b or c:
            self.refresh()
            self.refresh_neighbors(True)
            MapAPI.instance.schedule_map_refresh(False)
