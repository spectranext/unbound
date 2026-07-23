from typing import TYPE_CHECKING, Optional, Generator, Dict

from .. api.block import BlockObject
from .. api.device import DeviceAPISession, YieldAndClose, DeviceAPIHandler
from .. api.object import ObjectAPI
from . power import PowerConsumer
from .. api.query import QueryResponse, NothingQueryResponse, OPT
from . import BaseItem, BaseInstance
from .. import blocks, MapAPI
from .. import loc
from . device import Device, DeviceQueryResponseOptions

if TYPE_CHECKING:
    from .. team import Team


class DoorQueryResponse(QueryResponse):
    def __init__(self, c: 'DoorBaseInstance', player: ObjectAPI):
        super().__init__(b"", c.device.get_hostname())
        self.c = c
        self.player = player

        self.options = []

        if c.has_power:
            if c.closed:
                self.options.extend([
                    OPT(loc.DOOR_OPEN, self.open),
                ])
            else:
                self.options.extend([
                    OPT(loc.DOOR_CLOSE, self.close),
                ])
        else:
            self.options.extend([
                OPT(loc.DOOR_NO_POWER),
            ])

        self.options.extend(DeviceQueryResponseOptions.yield_options(c, player))
        self.actions = [loc.OK.encode()]

    def open(self, action: bytes) -> Optional[QueryResponse]:
        self.c.set_closed(False)
        return None

    def close(self, action: bytes) -> Optional[QueryResponse]:
        self.c.set_closed(True)
        return None


class DoorBaseInstance(BaseInstance, Device, DeviceAPIHandler, PowerConsumer):
    def __init__(self, x: int, y: int, prototype: 'DoorBaseItem', team: Optional['Team'], power_consumption: int, **kwargs):
        BaseInstance.__init__(self, x, y, prototype, team, **kwargs)
        Device.__init__(self, team.team_id if team else 0, b"door", listen=self)
        PowerConsumer.__init__(self)
        self.closed = True
        self.has_power = False
        self.power_consumption = power_consumption

    def get_x(self) -> int:
        return self.x

    def get_y(self) -> int:
        return self.y

    def serialize(self) -> Dict[bytes, bytes]:
        d = super().serialize()
        self.dev_serialize(d)
        return d

    def deserialize(self, data: Dict[bytes, bytes]):
        super().deserialize(data)
        self.dev_deserialize(data)

    def on_update(self):
        super().on_update()
        self.has_power = self.get_power_equilibrium() > 0

    def get_consumer_power(self) -> int:
        return self.power_consumption

    def consumes_power(self) -> bool:
        return True

    def on_destroy(self):
        super().on_destroy()
        self.device_destroy()

    def get_frame0_block(self) -> 'BlockObject':
        return self.get_block(0, 1)

    def get_frame1_block(self) -> 'BlockObject':
        return self.get_block(0, 2)

    def set_closed(self, closed: bool):
        if closed == self.closed:
            return
        if closed:
            self.set_block_code(0, 1, blocks.DOOR_FIELD)
            self.set_block_code(0, 2, blocks.DOOR_FIELD)
        else:
            self.set_block_code(0, 1, blocks.EMPTY)
            self.set_block_code(0, 2, blocks.EMPTY)
        MapAPI.instance.schedule_map_refresh(False)
        self.closed = closed

    def on_device_session(self, session: DeviceAPISession) -> Generator[bytes, bytes, None]:
        action: bytes = (yield)
        if action == b"open":
            self.set_closed(False)
            yield YieldAndClose(b"OK")
        elif action == b"close":
            self.set_closed(True)
            yield YieldAndClose(b"OK")
        else:
            yield YieldAndClose(b"?")

    def player_within_query_distance(self, player: ObjectAPI) -> bool:
        px = player.get_x()
        dx = px - self.x
        return -2 <= dx <= 1

    def query_instance(self, player: ObjectAPI) -> Optional['QueryResponse']:
        if not self.player_within_query_distance(player):
            return None
        self.set_closed(not self.closed)
        return NothingQueryResponse()


class DoorBaseItem(BaseItem):
    def __init__(self, identity: str):
        super().__init__(identity)
        self.power_consumption = 1

    def parse(self, v):
        super().parse(v)
        if "power_consumption" in v:
            self.power_consumption = v["power_consumption"]

    def get_instance(self, x: int, y: int, team: Optional['Team'], **kwargs) -> BaseInstance:
        return DoorBaseInstance(x, y, self, team, self.power_consumption, **kwargs)
