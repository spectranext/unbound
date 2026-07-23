import math
import random
import time
from collections import deque
from typing import Dict, Generator, List, Optional, Tuple

from . import BotPlayerObject
from .. api.object import ObjectAPI
from .. api.map import MapAPI
from .. import items

from ..tuning import Tuning


class BotGroup(object):
    PLAYER_AVOID_RADIUS = 24.0
    WALL_MARGIN = 2.0
    DISPERSION_MIN_WIDTH = 20.0
    WAVE_AMPLITUDE = 5.0
    WAVE_SPEED = 0.55
    WAVE_PHASE_SHIFT = 0.8
    FOLLOW_PLAYER_WEIGHT = 0.9
    SEPARATION_MIN = 4.0
    _slimes: List["Slime"] = []

    @classmethod
    def register(cls, slime: "Slime"):
        if slime not in cls._slimes:
            cls._slimes.append(slime)

    @classmethod
    def unregister(cls, slime: "Slime"):
        if slime in cls._slimes:
            cls._slimes.remove(slime)

    @classmethod
    def _active_slimes(cls) -> List["Slime"]:
        active: List["Slime"] = []
        for slime in list(cls._slimes):
            if slime is None:
                continue
            if slime.get_id() == 0:
                continue
            if not slime.is_alive():
                continue
            active.append(slime)
        active.sort(key=lambda s: s.get_id())
        return active

    @classmethod
    def _x_bounds(cls, server_map: MapAPI) -> Tuple[float, float]:
        left = cls.WALL_MARGIN
        right = max(left + 1.0, float(server_map.get_width()) - (cls.WALL_MARGIN + 1.0))
        return left, right

    @classmethod
    def _centers_for_slots(cls, server_map: MapAPI, slots: int) -> List[float]:
        slots = max(1, slots)
        left, right = cls._x_bounds(server_map)
        span = max(1.0, right - left)
        step = span / float(slots)
        return [left + ((i + 0.5) * step) for i in range(slots)]

    @classmethod
    def _wave_offset(cls, slot: int, width: float, now: float) -> float:
        max_amp = max(0.0, (width * 0.25) - 1.0)
        amp = min(cls.WAVE_AMPLITUDE, max_amp)
        if amp <= 0.0:
            return 0.0
        phase = (now * cls.WAVE_SPEED) + (slot * cls.WAVE_PHASE_SHIFT)
        return math.sin(phase) * amp

    @classmethod
    def _slot_layout(cls, server_map: MapAPI, active: List["Slime"]) -> Dict[int, Tuple[float, float, float]]:
        left, right = cls._x_bounds(server_map)
        slots = max(1, max(len(active), Tuning.SLIME_COUNT))
        centers = cls._centers_for_slots(server_map, slots)
        span = max(1.0, right - left)
        step = span / float(slots)
        half_width = max(cls.DISPERSION_MIN_WIDTH / 2.0, step * 0.6)
        now = time.time()
        layout: Dict[int, Tuple[float, float, float]] = {}

        for idx, slime in enumerate(active):
            center = centers[min(idx, len(centers) - 1)] + cls._wave_offset(idx, half_width * 2.0, now)
            center = max(left, min(right, center))
            local_left = max(left, center - half_width)
            local_right = min(right, center + half_width)
            if local_left > local_right:
                local_left = local_right = center
            layout[slime.get_id()] = center, local_left, local_right
        return layout

    @classmethod
    def control_target_x(cls, slime: "Slime", server_map: MapAPI, requested_x: float) -> float:
        active = cls._active_slimes()
        if not active:
            return requested_x

        layout = cls._slot_layout(server_map, active)
        slot = layout.get(slime.get_id())
        if slot is None:
            return requested_x

        center, left, right = slot
        guided = center + ((requested_x - center) * cls.FOLLOW_PLAYER_WEIGHT)
        guided = max(left, min(right, guided))

        nearest: Optional["Slime"] = None
        nearest_dist = 9999.0
        for other in active:
            if other is slime:
                continue
            dist = abs(float(other.get_x()) - float(slime.get_x()))
            if dist < nearest_dist:
                nearest_dist = dist
                nearest = other

        if nearest is not None and nearest_dist < cls.SEPARATION_MIN:
            if float(slime.get_x()) <= float(nearest.get_x()):
                guided -= (cls.SEPARATION_MIN - nearest_dist)
            else:
                guided += (cls.SEPARATION_MIN - nearest_dist)

        return max(left, min(right, guided))


class Slime(BotPlayerObject):
    SLIME = b"slime"
    SPIDER = b"spider"
    COUNT = 0

    NOTICE_RANGE_X = 14
    NOTICE_RANGE_Y = 6
    ATTACK_COOLDOWN_SECONDS = 1.0
    RUN_AWAY_AFTER_HIT_SECONDS = 1.5
    RUN_AWAY_TARGET_DISTANCE = 8.0
    ROAM_TRAVEL_MIN = 8.0
    ROAM_TRAVEL_MAX = 16.0
    MOVE_PHASE_MIN_SECONDS = 2.0
    MOVE_PHASE_MAX_SECONDS = 4.5
    SLEEP_PHASE_MIN_SECONDS = 1.5
    SLEEP_PHASE_MAX_SECONDS = 3.5
    STUCK_TIMEOUT_SECONDS = 3.0
    STUCK_POSITION_EPSILON = 0.05
    REACHABILITY_MARGIN_X = 8
    REACHABILITY_MARGIN_Y = 8
    UNREACHABLE_TARGET_BLACKLIST_SECONDS = 2.0

    def __init__(self, damage_value: int, health_value: int, data_entry: bytes, move_entry: bytes):
        BotPlayerObject.__init__(self, 0, data_entry, move_entry, None)
        self.attack_cooldown_until = 0.0
        self.run_away_until = 0.0
        self.damage_value = damage_value
        self.health = health_value
        self.roam_target_x = 0.0
        self.move_phase_until = 0.0
        self.sleep_phase_until = 0.0
        self._stuck_x: Optional[float] = None
        self._stuck_y: Optional[float] = None
        self._last_movement_progress_at = time.time()
        self._unreachable_target_until: Dict[int, float] = {}
        self._tracked_alive = True
        Slime.COUNT += 1
        BotGroup.register(self)

    def get_team_id(self) -> int:
        return 0

    def on_killed(self):
        super().on_killed()
        self._untrack_slime()

    def identity(self) -> bytes:
        return Slime.SLIME

    @classmethod
    def _find_spawn_y(cls, server_map: MapAPI, x: int) -> Optional[int]:
        x2 = min(server_map.get_width() - 1, x + 1)
        for y in range(0, server_map.get_height()):
            b = server_map.get_block(x, y)
            b1 = server_map.get_block(x2, y)
            if (b and b.blocking()) or (b1 and b1.blocking()):
                return y - 2
        return None

    @classmethod
    def _is_spawn_near_player(cls, server_map: MapAPI, x: int) -> bool:
        from ..player import PlayerObject

        radius = int(BotGroup.PLAYER_AVOID_RADIUS)
        nearby = server_map.query_objects(max(0, x - radius), 0, (radius * 2) + 1, server_map.get_height())
        for o in nearby:
            if not isinstance(o, PlayerObject):
                continue
            if not o.is_alive():
                continue
            if abs(float(o.get_x()) - float(x)) <= BotGroup.PLAYER_AVOID_RADIUS:
                return True
        return False

    @classmethod
    def _occupied_centers(cls, server_map: MapAPI) -> List[float]:
        active = BotGroup._active_slimes()
        slots = max(1, Tuning.SLIME_COUNT)
        centers = BotGroup._centers_for_slots(server_map, slots)
        if not active:
            return centers

        spacing = max(1.0, (centers[-1] - centers[0]) / max(1.0, float(len(centers))))
        occupancy = [False] * len(centers)
        for slime in active:
            sx = float(slime.get_x())
            nearest = min(range(len(centers)), key=lambda i: abs(centers[i] - sx))
            if abs(centers[nearest] - sx) <= spacing * 0.8:
                occupancy[nearest] = True

        return [c for i, c in enumerate(centers) if not occupancy[i]]

    @classmethod
    def spawn_missing(cls, server_map: MapAPI):
        missing = max(0, Tuning.SLIME_COUNT - cls.COUNT)
        if missing == 0:
            return

        candidates = cls._occupied_centers(server_map)
        if not candidates:
            return

        spawned = 0
        width = server_map.get_width()

        for center in candidates:
            if spawned >= missing:
                break

            center_x = int(max(1, min(width - 2, round(center))))
            try_offsets = [0, -2, 2, -4, 4, -6, 6, -8, 8]

            selected_x: Optional[int] = None
            selected_y: Optional[int] = None

            for off in try_offsets:
                x = max(1, min(width - 2, center_x + off))
                if cls._is_spawn_near_player(server_map, x):
                    continue
                y = cls._find_spawn_y(server_map, x)
                if y is None:
                    continue
                selected_x = x
                selected_y = y
                break

            if selected_x is None or selected_y is None:
                continue

            server_map.spawn_object(selected_x, selected_y, cls.SLIME)
            spawned += 1

    def generate_drop(self) -> Generator[Tuple[items.Item, int, float], None, None]:
        yield items.get_item("meat"), 1, 1.0
        if random.randint(0, 100) > 60:
            yield items.get_item("meat"), 1, 1.0

    def should_self_destroy(self, server_map: MapAPI):
        return Tuning.SLIME_NIGHT_ONLY and server_map.is_day()

    def on_update(self, server_map: MapAPI):
        super().on_update(server_map)

        if self.get_id() == 0 or not self.is_alive():
            return

        if self.should_self_destroy(server_map):
            self._untrack_slime()
            self.destroy()
            return

        player = self._find_target_player(server_map)
        now = time.time()
        if player is not None and player.is_alive():
            self.sleep_phase_until = 0.0
            self.move_phase_until = now + random.uniform(
                Slime.MOVE_PHASE_MIN_SECONDS, Slime.MOVE_PHASE_MAX_SECONDS
            )
            if now < self.run_away_until:
                if self.get_x() >= player.get_x():
                    target_x = float(self.get_x()) + Slime.RUN_AWAY_TARGET_DISTANCE
                else:
                    target_x = float(self.get_x()) - Slime.RUN_AWAY_TARGET_DISTANCE
                target_x = max(1.0, min(float(server_map.get_width() - 2), target_x))
            else:
                target_x = float(player.get_x())
        else:
            target_x = self._roam_target_x(server_map, now)

        target_x = self._control_target_x(server_map, target_x)

        movement_expected = self._move_towards_x(server_map, target_x)
        self._check_for_stuck(now, movement_expected)

    def on_contact(self, server_map: 'MapAPI', o: 'ObjectAPI'):
        if isinstance(o, Slime):
            return
        if isinstance(o, BotPlayerObject):
            return
        now = time.time()
        if now < self.attack_cooldown_until:
            return
        o.damage(self.damage_value, owner=self)
        self.attack_cooldown_until = now + Slime.ATTACK_COOLDOWN_SECONDS
        self.run_away_until = now + Slime.RUN_AWAY_AFTER_HIT_SECONDS

    def _roam_target_x(self, server_map: MapAPI, now: float) -> float:
        if now < self.sleep_phase_until:
            return float(self.get_x())

        if self.move_phase_until == 0.0:
            self.move_phase_until = now + random.uniform(
                Slime.MOVE_PHASE_MIN_SECONDS, Slime.MOVE_PHASE_MAX_SECONDS
            )
            self.roam_target_x = self._pick_roam_target_x(server_map)
            return self.roam_target_x

        if now >= self.move_phase_until:
            self.move_phase_until = 0.0
            self.sleep_phase_until = now + random.uniform(
                Slime.SLEEP_PHASE_MIN_SECONDS, Slime.SLEEP_PHASE_MAX_SECONDS
            )
            return float(self.get_x())

        if abs(self.roam_target_x - float(self.get_x())) <= 0.75:
            self.roam_target_x = self._pick_roam_target_x(server_map)

        return self.roam_target_x

    def _pick_roam_target_x(self, server_map: MapAPI) -> float:
        direction = 1.0 if random.random() >= 0.5 else -1.0
        distance = random.uniform(Slime.ROAM_TRAVEL_MIN, Slime.ROAM_TRAVEL_MAX)
        target = float(self.get_x()) + (direction * distance)
        return max(1.0, min(float(server_map.get_width() - 2), target))

    def _control_target_x(self, server_map: MapAPI, target_x: float) -> float:
        return BotGroup.control_target_x(self, server_map, target_x)

    def _find_nearby_player(self, server_map: MapAPI) -> Optional[ObjectAPI]:
        from .. player import PlayerObject

        x = int(self.get_x())
        y = int(self.get_y())
        x_from = max(0, x - Slime.NOTICE_RANGE_X)
        y_from = max(0, y - Slime.NOTICE_RANGE_Y)
        x_to = min(server_map.get_width() - 1, x + Slime.NOTICE_RANGE_X)
        y_to = min(server_map.get_height() - 1, y + Slime.NOTICE_RANGE_Y)
        nearby = server_map.query_objects(x_from, y_from, x_to - x_from, y_to - y_from)

        best: Optional[ObjectAPI] = None
        best_dist = 10_000.0
        for o in nearby:
            if o == self:
                continue
            if not isinstance(o, PlayerObject):
                continue
            if not o.is_alive():
                continue
            dx = float(o.get_x()) - float(self.get_x())
            dy = float(o.get_y()) - float(self.get_y())
            dist = (dx * dx) + (dy * dy)
            if dist < best_dist:
                best_dist = dist
                best = o
        return best

    def _find_target_player(self, server_map: MapAPI) -> Optional[ObjectAPI]:
        player = self._find_nearby_player(server_map)
        if player is None:
            return None
        now = time.time()
        target_id = player.get_id()
        if self._is_target_blacklisted(target_id, now):
            return None
        if not self._can_reach_object(server_map, player):
            self._blacklist_unreachable_target(target_id, now)
            return None
        return player

    def _is_target_blacklisted(self, target_id: int, now: float) -> bool:
        blacklist_until = self._unreachable_target_until.get(target_id)
        if blacklist_until is None:
            return False
        if now < blacklist_until:
            return True
        del self._unreachable_target_until[target_id]
        return False

    def _blacklist_unreachable_target(self, target_id: int, now: float):
        self._unreachable_target_until[target_id] = (
            now + Slime.UNREACHABLE_TARGET_BLACKLIST_SECONDS
        )

    def _can_path_through(self, server_map: MapAPI, x: int, y: int) -> bool:
        if x < 0 or y < 0 or x >= server_map.get_width() or y >= server_map.get_height():
            return False
        block = server_map.get_block(x, y)
        return bool(block is not None and not block.blocking())

    def _can_reach_object(self, server_map: MapAPI, obj: ObjectAPI) -> bool:
        start = (int(self.get_x()), int(self.get_y()))
        target = (int(obj.get_x()), int(obj.get_y()))
        if start == target:
            return True
        if not self._can_path_through(server_map, start[0], start[1]):
            return False
        if not self._can_path_through(server_map, target[0], target[1]):
            return False

        min_x = max(0, min(start[0], target[0]) - Slime.REACHABILITY_MARGIN_X)
        max_x = min(server_map.get_width() - 1, max(start[0], target[0]) + Slime.REACHABILITY_MARGIN_X)
        min_y = max(0, min(start[1], target[1]) - Slime.REACHABILITY_MARGIN_Y)
        max_y = min(server_map.get_height() - 1, max(start[1], target[1]) + Slime.REACHABILITY_MARGIN_Y)

        queue = deque([start])
        seen = {start}

        while queue:
            current = queue.popleft()
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                next_point = (current[0] + dx, current[1] + dy)
                if next_point in seen:
                    continue
                if next_point[0] < min_x or next_point[0] > max_x:
                    continue
                if next_point[1] < min_y or next_point[1] > max_y:
                    continue
                if not self._can_path_through(server_map, next_point[0], next_point[1]):
                    continue
                if next_point == target:
                    return True
                seen.add(next_point)
                queue.append(next_point)

        return False

    def _move_towards_x(self, server_map: MapAPI, target_x: float) -> bool:
        dx = float(target_x) - float(self.get_x())
        if abs(dx) <= 0.2:
            self.move_to(self.get_x(), self.get_y())
            return False

        step_dir = 1 if dx > 0 else -1
        desired_x = max(1.0, min(float(server_map.get_width() - 2), float(target_x)))

        if self.has_collision(ObjectAPI.COLLISION_BOTTOM):
            blocked_collision = (
                self.has_collision(ObjectAPI.COLLISION_RIGHT)
                if step_dir > 0 else
                self.has_collision(ObjectAPI.COLLISION_LEFT)
            )
            probe_x = int(self.get_x()) + (1 if step_dir > 0 else -1)
            probe_y = int(self.get_y())
            probe_x = max(0, min(server_map.get_width() - 1, probe_x))
            probe_y = max(0, min(server_map.get_height() - 1, probe_y))
            blocked_probe = bool(server_map.get_block(probe_x, probe_y).blocking())

            if blocked_collision or blocked_probe:
                self.jump(0.75)
                return True

        self.move_to(desired_x, self.get_y())
        return True

    def _check_for_stuck(self, now: float, movement_expected: bool) -> bool:
        x = float(self.get_x())
        y = float(self.get_y())
        if self._stuck_x is None or self._stuck_y is None:
            self._stuck_x = x
            self._stuck_y = y
            self._last_movement_progress_at = now
            return False

        moved = (
            abs(x - self._stuck_x) > self.STUCK_POSITION_EPSILON or
            abs(y - self._stuck_y) > self.STUCK_POSITION_EPSILON
        )

        if moved or not movement_expected:
            self._stuck_x = x
            self._stuck_y = y
            self._last_movement_progress_at = now
            return False

        if now - self._last_movement_progress_at <= self.STUCK_TIMEOUT_SECONDS:
            return False

        self._destroy_when_stuck()
        return True

    def _destroy_when_stuck(self):
        self._untrack_slime()
        self.destroy()

    def _untrack_slime(self):
        if not self._tracked_alive:
            return
        self._tracked_alive = False
        Slime.COUNT = max(0, Slime.COUNT - 1)
        BotGroup.unregister(self)
