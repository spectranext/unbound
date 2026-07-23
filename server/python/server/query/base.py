from typing import Optional
from .. api.map import MapAPI
from .. api.query import QueryResponse
from .. api.object import ObjectAPI
from .. import loc


class NoBaseResponse(QueryResponse):
    def __init__(self):
        super().__init__(b"", loc.PLAYER_NO_STRUCTS.encode())
        self.description = loc.PLAYER_NO_STRUCTS_DESC.encode()
        self.actions = [loc.OK.encode()]


def query_closest_base(p: ObjectAPI, distance: int = 0) -> Optional[QueryResponse]:
    r = MapAPI.instance.bases.query_closest(p, distance)
    if r is None:
        return None
    return r

