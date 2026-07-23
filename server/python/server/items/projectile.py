from typing import TYPE_CHECKING

import math

from . import Item
from .. api.map import MapAPI
from .. objects.rocket import RocketObject

if TYPE_CHECKING:
    from .. player import PlayerObject


class ProjectileItem(Item):
    def __init__(self, identity: str):
        super().__init__(identity)
        self.projectile: bytes = RocketObject.ROCKET
        self.damage_value: int = 0
        self.power = 3

    def parse(self, v):
        super().parse(v)
        if "projectile" in v:
            self.projectile = v["projectile"].encode()
        if "power" in v:
            self.power = v["power"]
        if "damage_value" in v:
            self.damage_value = int(v["damage_value"])

    def on_touch(self, p: 'PlayerObject', x: int, y: int) -> bool:
        # noinspection PyTypeChecker
        o: RocketObject = MapAPI.instance.spawn_object(p.get_x(), p.get_y(), self.projectile)
        o.owner = p
        o.team = p.get_team()
        o.damage_value = self.damage_value

        diff_x = (x - p.get_x() - 0.5)
        diff_y = (y - p.get_y() + 1)

        angle = math.atan2(diff_y, diff_x)

        o.set_speed(int(math.cos(angle) * self.power), int(math.sin(angle) * self.power))
        p.remove_from_inventory(self, 1)

        return True
