from typing import Dict, Tuple, Optional, Callable, TYPE_CHECKING

from . power import PowerNetworks
from .. api.object import ObjectAPI
from .. api.query import QueryResponse
from .. api.map import MapAPI


if TYPE_CHECKING:
    from . import BaseInstance


class Bases(object):
    DISTANCE_LIMIT = 32

    def __init__(self):
        self.entries: Dict[Tuple[int, int], 'BaseInstance'] = {}

    def add_base(self, x: int, y: int, b: 'BaseInstance'):
        self.entries[(x, y)] = b

    def update_bases(self):
        PowerNetworks.calculate_networks(self.entries.values())
        # Create a copy of entries to avoid RuntimeError if a base destroys itself during update
        bases_to_update = list(self.entries.values())
        for b in bases_to_update:
            b.on_update()

    def query_closest(self, p: ObjectAPI, distance: int = 0) -> Optional[QueryResponse]:
        x = p.get_x()
        y = p.get_y()
        dd = distance or Bases.DISTANCE_LIMIT
        distance = dd * dd
        result: Optional[Callable[[ObjectAPI], Optional['QueryResponse']]] = None
        for b in self.entries.values():
            dst = (x - b.x) * (x - b.x) + (y - b.y) * (y - b.y)
            if dst < distance:
                distance = dst
                result = b.query_instance
        for o in MapAPI.instance.query_objects(
                int(x - dd // 2), int(y - dd // 2), dd, dd):
            if not o.validate_player_query(p):
                continue
            dst = (x - o.get_x()) * (x - o.get_x()) + (y - o.get_y()) * (y - o.get_y())
            if dst < distance:
                distance = dst
                result = o.on_player_query
        if result is None:
            return None
        return result(p)

    def remove_base(self, x: int, y: int):
        del self.entries[(x, y)]
