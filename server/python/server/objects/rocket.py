from typing import Optional, TYPE_CHECKING

from .. api.map import MapAPI
from .. api.object import ObjectAPI
from .. effects import MapEffects

if TYPE_CHECKING:
    from .. team import Team


class RocketObject(ObjectAPI):
    ROCKET = b"rock"

    def __init__(self):
        super().__init__(ObjectAPI.OBJECT_TYPE_SPRITE | ObjectAPI.OBJECT_TYPE_SLOW | ObjectAPI.OBJECT_TYPE_COLLIDER,
                         ObjectAPI.DATA_ENTRY_ROCKET, ObjectAPI.DATA_ENTRY_ROCKET)
        self.damage_value = 0
        self.owner: Optional['ObjectAPI'] = None
        self.team: Optional['Team'] = None

    def identity(self) -> bytes:
        return RocketObject.ROCKET

    def get_team(self) -> Optional['Team']:
        return self.team

    def set_team(self, team: 'Team'):
        self.team = team

    def on_block_collide(self, server_map: 'MapAPI'):

        for o in MapAPI.instance.query_objects(int(self.get_x() - 3), int(self.get_y() - 3), 6, 6):
            if o == self:
                continue
            if o != self.owner:
                t = o.get_team()
                if t and t == self.get_team():
                    continue
            o.damage(self.damage_value, "rocket", self.owner)

        MapAPI.instance.send_effect(self.get_x(), self.get_y(), MapEffects.EXPLOSION)

        self.destroy()

    def on_update(self, server_map: 'MapAPI'):
        super().on_update(server_map)
        speed_x = self.get_speed_x()
        speed_y = self.get_speed_y()
        map_x = 1 if speed_x > 0 else (-1 if speed_x < 0 else 0)
        map_y = 10 if speed_y > 0 else (-10 if speed_y < 0 else 0)
        offsets = {
            -10: 0,
            -9: 32,
            1: 64,
            11: 96,
            10: 128,
            9: 160,
            -1: 192,
            -11: 224
        }
        offset = offsets.get(map_x + map_y, 0)
        self.set_sprite_offset(offset)
