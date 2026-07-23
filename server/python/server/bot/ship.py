from typing import Optional, Generator, Tuple, Callable
from . import BotPlayerObject
from .. api.object import ObjectAPI
from .. api.map import MapAPI
from .. inventory import Inventory
from .. bases import BaseBlockObject


class Ship(BotPlayerObject):
    SHIP1 = b"ship1"
    FLAMES = [b"FLAMES1", b"FLAMES2"]

    def __init__(self, identity: bytes, data_entry: bytes, move_entry: bytes):
        BotPlayerObject.__init__(self, ObjectAPI.OBJECT_TYPE_STATIC | ObjectAPI.OBJECT_TYPE_SPRITE,
                         data_entry, move_entry, None)
        self._identity = identity
        self.inventory = Inventory()
        self.neutral = True
        self.cooldown = None
        self._on_landed: Optional[Callable] = None
        self._on_fled: Optional[Callable] = None
        self.landed = False
        self._up = False
        self._slide_once = True

    def identity(self) -> bytes:
        return self._identity

    def set_on_landed(self, cb: Callable):
        self._on_landed = cb

    def set_on_fled(self, cb: Callable):
        self._on_fled = cb

    def on_killed(self):
        pass

    def damage(self, value: int, reason: Optional[str] = None, owner: Optional['ObjectAPI'] = None) -> bool:
        return False

    def fly_off(self):
        self._up = True
        self.set_motion_profile(ObjectAPI.MOTION_PROFILE_ACCELERATING)

    def get_inventory(self) -> Optional[Inventory]:
        return self.inventory

    def bottom_is_blocking(self, server_map: MapAPI):
        b = server_map.get_block(int(self.get_x()), int(self.get_y() + 1))
        if b and b.blocking():
            return True
        if isinstance(b, BaseBlockObject):
            return True
        b = server_map.get_block(int(self.get_x() + 1), int(self.get_y() + 1))
        if b and b.blocking():
            return True
        if isinstance(b, BaseBlockObject):
            return True
        return False

    def get_landing_target_y(self, server_map: MapAPI) -> float:
        start_y = int(self.get_y()) + 1
        max_y = MapAPI.instance.get_height()

        for y in range(start_y, max_y + 1):
            left = server_map.get_block(int(self.get_x()), y)
            right = server_map.get_block(int(self.get_x() + 1), y)

            if (left and left.blocking()) or isinstance(left, BaseBlockObject):
                return float(y - 1)
            if (right and right.blocking()) or isinstance(right, BaseBlockObject):
                return float(y - 1)

        return float(max_y - 1)

    def slide(self, server_map: 'MapAPI'):
        if self._up:
            if self.get_y() <= 0:
                if self._on_fled:
                    self._on_fled()
                    self._on_fled = None
                self.destroy()
            else:
                self.move_to(self.get_x(), 0., 0)
        else:
            if self.landed:
                pass
            else:
                self.set_motion_profile(ObjectAPI.MOTION_PROFILE_SLOWING_DOWN)
                if self.bottom_is_blocking(server_map):
                    self.set_motion_profile(ObjectAPI.MOTION_PROFILE_NONE)
                    self.move_to(self.get_x(), self.get_y(), 0)
                    self.landed = True
                    if self._on_landed:
                        self._on_landed()
                        self._on_landed = None
                else:
                    self.move_to(self.get_x(), self.get_landing_target_y(server_map), 0)

    def on_update(self, server_map: 'MapAPI'):
        super().on_update(server_map)
        self.slide(server_map)
