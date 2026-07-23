from typing import Dict, Optional, TYPE_CHECKING

from . import BaseItem, BaseInstance
from . power import PowerConsumer
from .. api.map import MapAPI
from .. api.object import ObjectAPI
from .. api.query import QueryResponse, OPT
from .. import blocks, items, loc

if TYPE_CHECKING:
    from .. team import Team


class LightBaseQueryResponse(QueryResponse):
    def __init__(self, player: ObjectAPI, b: 'LightBaseInstance'):
        super().__init__(b"", b"Light")
        self.player = player
        self.b = b
        self.description = "Battery: {0}/{1}".format(int(b.battery), b.battery_capacity).encode()
        self.options = [
            OPT("Recharge", self.recharge),
            OPT("Dismantle", self.dismantle),
        ]
        self.actions = [loc.SELECT.encode(), loc.OK.encode()]

    def recharge(self, action: bytes) -> Optional[QueryResponse]:
        was_lit = self.b.has_light()
        self.b.battery = self.b.battery_capacity
        self.b.sync_light_block()
        if not was_lit:
            MapAPI.instance.schedule_map_refresh(False)
        return LightBaseQueryResponse(self.player, self.b)

    def dismantle(self, action: bytes) -> Optional[QueryResponse]:
        spawner = self.b.prototype.get_spawner()
        if spawner is not None:
            self.player.get_team().inventory.add_item(spawner, 1, 1.)
        self.b.destroy()
        MapAPI.instance.schedule_map_refresh(False)
        return None


class LightBaseInstance(BaseInstance, PowerConsumer):
    def __init__(self, x: int, y: int, prototype: 'LightBaseItem', team: Optional['Team'], **kwargs):
        BaseInstance.__init__(self, x, y, prototype, team, **kwargs)
        PowerConsumer.__init__(self)
        self.power_consumption = prototype.power_consumption
        self.battery_capacity = prototype.battery_capacity
        self.charge_rate = prototype.charge_rate
        self.drain_rate = prototype.drain_rate
        self.battery = self.battery_capacity
        self.lit = False

    def serialize(self) -> Dict[bytes, bytes]:
        d = super().serialize()
        d[b"battery"] = int(self.battery).to_bytes(4, "little")
        return d

    def deserialize(self, data: Dict[bytes, bytes]):
        super().deserialize(data)
        if b"battery" in data:
            self.battery = min(self.battery_capacity, int.from_bytes(data[b"battery"], "little"))
        else:
            self.battery = self.battery_capacity

    def get_x(self) -> int:
        return self.x

    def get_y(self) -> int:
        return self.y

    def on_init(self):
        self.sync_light_block()

    def get_consumer_power(self) -> int:
        return self.power_consumption

    def consumes_power(self) -> bool:
        return self.battery < self.battery_capacity

    def has_light(self) -> bool:
        return self.battery > 0

    def has_charge_power(self) -> bool:
        return self.has_power_producers() and self.get_power_equilibrium() >= 0

    def get_light_source(self):
        return self.x, self.y - 1

    def sync_light_block(self):
        lit = self.has_light()
        code = blocks.LIGHT_TOP if lit else 0
        b = self.get_block(0, 1)
        if b is None:
            return
        if b.code != code:
            b.code = code
            MapAPI.instance.update_block(b.x, b.y, True)
        self.lit = lit

    def on_update(self):
        powered = self.has_charge_power()
        if powered and self.battery < self.battery_capacity:
            self.battery = min(self.battery_capacity, self.battery + self.charge_rate)
        elif not powered and self.battery > 0:
            self.battery = max(0, self.battery - self.drain_rate)

        was_lit = self.lit
        self.sync_light_block()
        if was_lit != self.lit:
            MapAPI.instance.schedule_map_refresh(False)

    def query_instance(self, player: ObjectAPI) -> Optional[QueryResponse]:
        return LightBaseQueryResponse(player, self)

    def on_touch(self, player: ObjectAPI) -> bool:
        from .. player import PlayerObject
        if isinstance(player, PlayerObject):
            player.client.force_query(self.query_instance(player))
        return True


class LightBaseItem(BaseItem):
    def __init__(self, identity: str):
        super().__init__(identity)
        self.power_consumption = 5
        self.battery_capacity = 300
        self.charge_rate = 2
        self.drain_rate = 1
        self.spawner_id = "{0}_s".format(identity)

    def parse(self, v):
        super().parse(v)
        if "power_consumption" in v:
            self.power_consumption = v["power_consumption"]
        if "battery_capacity" in v:
            self.battery_capacity = v["battery_capacity"]
        if "charge_rate" in v:
            self.charge_rate = v["charge_rate"]
        if "drain_rate" in v:
            self.drain_rate = v["drain_rate"]

    def get_spawner(self) -> Optional[items.Item]:
        return items.Item.ITEMS.get(self.spawner_id)

    def get_instance(self, x: int, y: int, team: Optional['Team'], **kwargs) -> BaseInstance:
        return LightBaseInstance(x, y, self, team, **kwargs)
