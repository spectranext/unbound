import math
import random
from typing import Optional

from . slime import Slime
from .. import MapAPI
from .. api.object import ObjectAPI


class Spider(Slime):
    ATTACK_RANGE = 8
    PATROL_RADIUS = 5.0
    RECALL_DISTANCE = 1.5

    def __init__(self, damage_value: int, health_value: int, data_entry: bytes, move_entry: bytes):
        super().__init__(damage_value, health_value, data_entry, move_entry)
        self.nest = None
        self.recalling_to_nest = False

    def identity(self) -> bytes:
        return Slime.SPIDER

    def bind_nest(self, nest):
        self.nest = nest
        self.roam_target_x = float(nest.x)

    def recall_to_nest(self):
        self.recalling_to_nest = True
        self.sleep_phase_until = 0.0
        self.move_phase_until = 0.0

    def should_self_destroy(self, server_map: MapAPI):
        return False

    def damage(self, value: int, reason: Optional[str] = None, owner: Optional['ObjectAPI'] = None) -> bool:
        if self.nest is not None and not self.is_flashing():
            self.nest.request_reinforcement()
        return super().damage(value, reason=reason, owner=owner)

    def on_killed(self):
        if self.nest is not None:
            nest = self.nest
            self.nest = None
            nest.spider_killed(self.get_id())
        super().on_killed()

    def on_update(self, server_map: MapAPI):
        if self.nest is not None and self.recalling_to_nest and self.is_alive():
            distance = math.hypot(
                float(self.get_x()) - self.nest.x,
                float(self.get_y()) - self.nest.y)
            if distance <= self.RECALL_DISTANCE:
                nest = self.nest
                self.nest = None
                self._untrack_slime()
                self.destroy()
                nest.spider_returned(self.get_id())
                return
        super().on_update(server_map)

    def _roam_target_x(self, server_map: MapAPI, now: float) -> float:
        if self.nest is not None and self.recalling_to_nest:
            return float(self.nest.x)
        return super()._roam_target_x(server_map, now)

    def _pick_roam_target_x(self, server_map: MapAPI) -> float:
        if self.nest is None:
            return super()._pick_roam_target_x(server_map)
        target_x = self.nest.x + random.uniform(-self.PATROL_RADIUS, self.PATROL_RADIUS)
        return max(1.0, min(float(server_map.get_width() - 2), target_x))

    def _find_target_player(self, server_map: MapAPI) -> Optional[ObjectAPI]:
        from .. player import PlayerObject

        if self.nest is None:
            return super()._find_target_player(server_map)

        x, y = self.nest._center()
        radius = self.ATTACK_RANGE
        x_from = max(0, int(x - radius))
        y_from = max(0, int(y - radius))
        x_to = min(server_map.get_width() - 1, int(x + radius))
        y_to = min(server_map.get_height() - 1, int(y + radius))
        nearby = server_map.query_objects(x_from, y_from, x_to - x_from, y_to - y_from)

        best = None
        best_dist = float("inf")
        for obj in nearby:
            if not isinstance(obj, PlayerObject) or not obj.is_alive():
                continue
            distance = math.hypot(float(obj.get_x()) - x, float(obj.get_y()) - y)
            if distance > radius or distance >= best_dist:
                continue
            best = obj
            best_dist = distance
        return best

    def _control_target_x(self, server_map: MapAPI, target_x: float) -> float:
        if self.nest is not None:
            return target_x
        return super()._control_target_x(server_map, target_x)

    def _destroy_when_stuck(self):
        if self.nest is not None:
            nest = self.nest
            self.nest = None
            nest.spider_killed(self.get_id())
        super()._destroy_when_stuck()
