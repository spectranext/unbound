import random

from typing import List, Dict
from . api.map import MapAPI
from . api.block import NeighboringSet, NeighboringBlockObject
from . import items


class GroundBlockObject(NeighboringBlockObject):
    def __init__(self, neighboring_set: NeighboringSet, grass: List[items.Item] = None,
                 proto: Dict[bytes, bytes] = None, **kwargs):
        super().__init__(neighboring_set, proto=proto, **kwargs)
        self.grass = grass
        self.grow_scheduled = False
        self.something_above = None

    def grow(self):
        self.grow_scheduled = False

        if not self.grass:
            return

        from . blockspawn import BlockSpawn

        b = MapAPI.instance.get_block(self.x, self.y - 1)
        if b is None:
            return

        if b.code:
            return

        b = BlockSpawn.create_block(random.choice(self.grass))
        b.copy_light(self)
        MapAPI.instance.set_block(self.x, self.y - 1, b, True)

    def refresh(self):
        super().refresh()

        # need both left and right support to grow
        if self.neighbor_code & 10 != 10:
            self.remove_grass(True)
            return

        if self.grow_scheduled:
            return

        something_above = False

        b = MapAPI.instance.get_block(self.x, self.y - 1)
        if b and b.code:
            something_above = True
        else:
            for y in range(self.y - 2, 0, -1):
                b = MapAPI.instance.get_block(self.x, y)
                if b and b.blocking():
                    something_above = True
                    break

        if self.something_above == something_above:
            return

        self.something_above = something_above

        if something_above:
            return

        if random.randint(0, 8) > 4:
            return

        self.grow_scheduled = True

        time = random.randint(2000, 10000)
        MapAPI.instance.schedule_block_method(self.x, self.y, time, b"grow")

    def remove_grass(self, notify: bool):
        from . blockspawn import BlockSpawn

        b = MapAPI.instance.get_block(self.x, self.y - 1)
        if b is None:
            return
        if b.item in self.grass:
            MapAPI.instance.set_block(
                self.x, self.y - 1, BlockSpawn.create_block(items.NOTHING), notify)

    def release(self, notify: bool):
        self.remove_grass(notify)
        super().release(notify)
