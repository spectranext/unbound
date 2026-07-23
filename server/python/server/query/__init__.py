from .. import loc
from .. api.object import ObjectAPI
from .. api.query import NothingQueryResponse
from .. player import status, PlayerObject
from . import inventory
from . import base


def query(p: ObjectAPI, q: bytes, distance: int = 0):
    if q == b"inventory":
        return inventory.InventoryQueryResponse(q, p, p.get_team().inventory, loc.BASE_STORAGE)
    if isinstance(p, PlayerObject):
        if q == b"status":
            if not p.client.tutorial:
                return status.StatusQueryResponse(q, p)

            s = base.query_closest_base(p, distance)
            if s:
                if isinstance(s, NothingQueryResponse):
                    return None
                return s
            return status.StatusQueryResponse(q, p)
    return None
