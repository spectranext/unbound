import math
import random
import time
from typing import Dict, List, Optional, Tuple, TYPE_CHECKING

from .. import items
from .. api.map import MapAPI
from .. api.touch import PostponedTouch
from .. blockspawn import BlockSpawn
from . import BaseInstance, BaseItem

if TYPE_CHECKING:
    from .. api.object import ObjectAPI
    from .. team import Team


class SpiderNestDismantle(PostponedTouch):
    DURATION_MS = 5000

    def __init__(self, nest: 'SpiderNestBaseInstance'):
        super().__init__(self.DURATION_MS)
        self.nest = nest
        self.started_at = time.time()
        self.deadline = self.started_at + (self.DURATION_MS / 1000.0)

    def update(self):
        registered_nest = MapAPI.instance.bases.entries.get((self.nest.x, self.nest.y))
        if registered_nest is not self.nest:
            return True
        if self.nest.state != self.nest.STATE_EXPOSED:
            return True

        now = time.time()
        if now >= self.deadline:
            self.nest.destroy()
            return True

        return int(12.0 * ((now - self.started_at) / (self.deadline - self.started_at)))


class SpiderNestBaseInstance(BaseInstance):
    STATE_SLEEPING = "sleeping"
    STATE_ARMED = "armed"
    STATE_EXPOSED = "exposed"
    WAKE_RADIUS = 12
    RECALL_RADIUS = 14
    MAX_SPIDERS = 2
    EXPOSED_SECONDS = 10.0
    SPAWN_LIMIT_BUMP = 120
    SPAWN_LIMIT_THRESHOLD = 240
    SCAN_INTERVAL_SECONDS = 0.5
    PATHFIND_CACHE_SECONDS = 5.0

    def __init__(self, x: int, y: int, prototype: 'BaseItem', team: Optional['Team'], **kwargs):
        super().__init__(x, y, prototype, team, **kwargs)
        self.state = self.STATE_SLEEPING
        self.spawn_limiter = 0
        self.wave_spawns = 0
        self.exposed_until = 0.0
        self.spider_ids: List[int] = []
        self.next_scan_at = 0.0
        self.player_pathfind_cache: Dict[int, Tuple[float, bool]] = {}

    def _center(self):
        return self.x + (self.prototype.width / 2.0), self.y - ((self.prototype.height - 1) / 2.0)

    def on_touch(self, player: 'ObjectAPI'):
        if self.state != self.STATE_EXPOSED:
            return True
        return SpiderNestDismantle(self)

    def _live_spiders(self):
        live = []
        for spider_id in self.spider_ids:
            spider = MapAPI.instance.get_object(spider_id)
            if spider is None or not spider.is_alive():
                continue
            live.append(spider)
        self.spider_ids = [spider.get_id() for spider in live]
        return live

    def _nearby_players(self, radius: int):
        from .. player import PlayerObject

        x, y = self._center()
        nearby = MapAPI.instance.query_objects(
            max(0, int(x - radius)),
            max(0, int(y - radius)),
            radius * 2,
            radius * 2)
        players = []
        for obj in nearby:
            if not isinstance(obj, PlayerObject):
                continue
            if not obj.is_alive():
                continue
            if math.hypot(float(obj.get_x()) - x, float(obj.get_y()) - y) <= radius:
                players.append(obj)
        return players

    def _pathfind_start(self) -> Optional[Tuple[int, int]]:
        from .. map.pathfind import find_nearest_open

        center_x, _ = self._center()
        return find_nearest_open(MapAPI.instance, (int(center_x), int(self._spawn_y())))

    def _can_reach_player(self, player: 'ObjectAPI', now: float) -> bool:
        player_id = player.get_id()
        cached = self.player_pathfind_cache.get(player_id)
        if cached is not None:
            expires_at, reachable = cached
            if now < expires_at:
                return reachable

        from .. map.pathfind import can_reach

        start = self._pathfind_start()
        target = (int(player.get_x()), int(player.get_y()))

        reachable = start is not None and can_reach(MapAPI.instance, start, target)
        self.player_pathfind_cache[player_id] = (now + self.PATHFIND_CACHE_SECONDS, reachable)
        return reachable

    def _reachable_nearby_players(self, radius: int, now: float):
        players = []
        for player in self._nearby_players(radius):
            if self._can_reach_player(player, now):
                players.append(player)

        for player_id, cached in list(self.player_pathfind_cache.items()):
            expires_at, _ = cached
            if now >= expires_at:
                del self.player_pathfind_cache[player_id]

        return players

    def _spawn_y(self):
        for y in range(self.y + 3, 1, -1):
            left = MapAPI.instance.get_block(self.x, y)
            right = MapAPI.instance.get_block(self.x + 1, y)
            if (left and left.blocking()) or (right and right.blocking()):
                return y - 2
        return self.y - 1

    def _spawn_spider(self) -> bool:
        if self.state == self.STATE_EXPOSED:
            return False
        if self.wave_spawns >= self.MAX_SPIDERS:
            return False
        if len(self._live_spiders()) >= self.MAX_SPIDERS:
            return False
        if self.spawn_limiter >= self.SPAWN_LIMIT_THRESHOLD:
            return False

        x, _ = self._center()
        spider = MapAPI.instance.spawn_object(
            x + random.choice((-1.0, 1.0)),
            self._spawn_y(),
            b"spider")
        spider.bind_nest(self)
        self.spider_ids.append(spider.get_id())
        self.wave_spawns += 1
        self.spawn_limiter += self.SPAWN_LIMIT_BUMP
        self.state = self.STATE_ARMED
        return True

    def request_reinforcement(self):
        self.spawn_limiter = min(self.SPAWN_LIMIT_THRESHOLD, self.spawn_limiter + self.SPAWN_LIMIT_BUMP)
        if self.state == self.STATE_EXPOSED:
            return
        if self.wave_spawns >= self.MAX_SPIDERS:
            return
        if len(self._live_spiders()) >= self.MAX_SPIDERS:
            return

        old_limiter = self.spawn_limiter
        self.spawn_limiter = min(self.spawn_limiter, self.SPAWN_LIMIT_THRESHOLD - 1)
        if not self._spawn_spider():
            self.spawn_limiter = old_limiter

    def spider_killed(self, spider_id: int):
        self.spider_ids = [tracked_id for tracked_id in self.spider_ids if tracked_id != spider_id]
        if self._live_spiders():
            return
        self.state = self.STATE_EXPOSED
        self.exposed_until = time.time() + self.EXPOSED_SECONDS

    def spider_returned(self, spider_id: int):
        self.spider_ids = [tracked_id for tracked_id in self.spider_ids if tracked_id != spider_id]
        if self._live_spiders():
            return
        self.state = self.STATE_SLEEPING
        self.wave_spawns = 0
        self.exposed_until = 0.0

    def _recall_spiders(self):
        for spider in self._live_spiders():
            spider.recall_to_nest()

    def on_update(self):
        super().on_update()
        self.spawn_limiter = max(0, self.spawn_limiter - 1)

        now = time.time()
        if self.state == self.STATE_EXPOSED:
            if now < self.exposed_until:
                return
            self.state = self.STATE_SLEEPING
            self.wave_spawns = 0
            self.exposed_until = 0.0

        if now < self.next_scan_at:
            return
        self.next_scan_at = now + self.SCAN_INTERVAL_SECONDS

        players = self._reachable_nearby_players(self.WAKE_RADIUS, now)
        live_spiders = self._live_spiders()

        if self.state == self.STATE_ARMED and self.wave_spawns and not live_spiders:
            self.state = self.STATE_EXPOSED
            self.exposed_until = now + self.EXPOSED_SECONDS
            return

        if players:
            if not live_spiders:
                self._spawn_spider()
            else:
                self.state = self.STATE_ARMED
            return

        if self.spawn_limiter == 0 and not self._reachable_nearby_players(self.RECALL_RADIUS, now):
            self._recall_spiders()
            if not self._live_spiders():
                self.state = self.STATE_SLEEPING
                self.wave_spawns = 0

    def on_destroy(self):
        super().on_destroy()
        for spider in self._live_spiders():
            spider._untrack_slime()
            spider.destroy()
        block = BlockSpawn.create_block(items.get("spidernest_s"))
        if block is not None:
            MapAPI.instance.set_block(self.x, self.y, block, True)
            MapAPI.instance.schedule_map_refresh(False)


class SpiderNestBaseItem(BaseItem):
    def get_instance(self, x: int, y: int, team: Optional['Team'], **kwargs) -> BaseInstance:
        return SpiderNestBaseInstance(x, y, self, team, **kwargs)
