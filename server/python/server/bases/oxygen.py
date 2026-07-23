from typing import Optional, TYPE_CHECKING, Dict, Generator
from datetime import timedelta
import time

from .. api.query import QueryResponse, DescriptionQueryResponse, OPT, NOACT
from .. api.object import ObjectAPI
from .. api.client import ClientAPI
from . inventory import InventoryQueryResponse, PlacingInventoryFilter, InventoryBaseInstance
from .. air import AirProducer
from .. import loc, icons, MapAPI

from . import BaseItem, BaseInstance
from .. import blocks

if TYPE_CHECKING:
    from .. team import Team
    from .. items import Item
    from .. player import PlayerObject


class OxygenPlacingInventoryFilter(PlacingInventoryFilter):
    def name(self) -> str:
        return "Oxygen"

    def filter(self, item: 'Item') -> bool:
        return item.power_source > 0


class OxygenStatusQueryResponse(QueryResponse):
    def __init__(self, player: ObjectAPI, bi: 'OxygenTankBaseInstance'):
        if bi.generating:
            status = loc.OXYGEN_TANK_GENERATING
        else:
            status = loc.OXYGEN_TANK_NOT_OPERATIONAL
            if not bi.has_pressure():
                status += "\n" + loc.OXYGEN_TANK_NO_PRESSURE
            if bi.get_potential_power() == 0:
                status += "\n" + loc.OXYGEN_TANK_NO_OXYGEN
        super().__init__(b"", loc.OXYGEN_TANK.encode())
        self.description = loc.OXYGEN_TANK_REFILL_DESC.format(
            status,
            str(timedelta(seconds=int(player.power * player.power_consumption))),
            str(int(bi.get_potential_power())),
        ).encode()

        self.player = player
        self.bi = bi
        self.options = [
            OPT(loc.OXYGEN_TANK_OPEN_INVENTORY, self.inventory, icon=icons.ICON_INVENTORY2),
        ]

        if not bi.has_pressure():
            self.options.append(
                OPT(loc.OXYGEN_TANK_NO_PRESSURE, NOACT(
                    DescriptionQueryResponse,
                    loc.OXYGEN_TANK_NO_PRESSURE.encode(),
                    loc.OXYGEN_TANK_NO_PRESSURE_DESC.encode()), icon=icons.ICON_ERROR),
            )

        if bi.get_potential_power() == 0:
            self.options.append(
                OPT(loc.OXYGEN_TANK_NO_OXYGEN, NOACT(
                    DescriptionQueryResponse,
                    loc.OXYGEN_TANK_NO_OXYGEN.encode(),
                    loc.OXYGEN_TANK_NO_OXYGEN_DESC.encode()), icon=icons.ICON_ERROR),
            )

        self.actions = [loc.OK.encode()]

    def refill(self, action: bytes) -> Optional[QueryResponse]:
        self.bi.refill(self.player)
        c = self.player.get_client()
        if c:
            o = int(100 * (self.player.power / self.player.get_max_power()))
            c.queue_notify(loc.OXYGEN_TANK_REFILLED.format(o).encode(), ClientAPI.NOTIFY_MESSAGE_COLOR_SUCCESS)
        return None

    def inventory(self, action: bytes) -> Optional[QueryResponse]:
        return InventoryQueryResponse(
            loc.OXYGEN_TANK_INVENTORY, None, self.player, self.bi.inventory,
            placing_filter=OxygenTankBaseInstance.OXYGEN_FILTER)


class OxygenTankBaseInstance(InventoryBaseInstance, AirProducer):
    OXYGEN_FILTER = OxygenPlacingInventoryFilter()

    def __init__(self, x: int, y: int, prototype: 'OxygenTankBaseItem', team: Optional['Team'], **kwargs):
        InventoryBaseInstance.__init__(self, x, y, prototype, team, **kwargs)
        AirProducer.__init__(self)
        self.power = 0
        self.air_block = prototype.air_block
        self.pressure_multiplier = prototype.pressure_multiplier
        self._yield = prototype._yield
        self.generating = False
        MapAPI.instance.air.register(self)

    def on_destroy(self):
        MapAPI.instance.air.unregister(self)
        super().on_destroy()

    def get_x(self) -> int:
        return self.x

    def get_y(self) -> int:
        return self.y

    def get_potential_power(self):
        pw = self.power
        for e in self.inventory.entries.values():
            if e.item.power_source == 0:
                continue
            pw += e.item.power_source * e.amount * e.health
        return pw

    def yield_power(self, amount) -> int:
        to_remove = []
        for e in self.inventory.entries.values():
            if e.item.power_source == 0:
                continue
            self.power += e.item.power_source * e.health * e.amount
            to_remove.append(e)

        for e in to_remove:
            self.inventory.remove_from_record(e, e.amount)

        if self.power == 0:
            return 0
        elif amount <= self.power:
            self.power -= amount
            return amount
        else:
            remainder = self.power
            self.power = 0
            return remainder

    def refill(self, player: ObjectAPI):
        player.power += self.yield_power(player.get_max_power() - player.power)

    def query_instance(self, player: ObjectAPI) -> Optional[QueryResponse]:
        return OxygenStatusQueryResponse(player, self)

    def on_update(self):
        super().on_update()
        should_generate = (self.get_potential_power() > 0) and (self.has_pressure())
        if should_generate != self.generating:
            self.generating = should_generate
            if self.generating:
                self.set_block_code(0, 1, blocks.OXYGEN_STATION_GENERATING_0)
            else:
                self.set_block_code(0, 1, blocks.OXYGEN_STATION_0)
        if self.generating:
            for xx, yy in self.get_pressure_zone_blocks():
                b = MapAPI.instance.get_block(xx, yy)
                if not b:
                    continue
                if b.is_air_full():
                    continue
                air = self.yield_power(1)
                if not air:
                    break
                b.inject_air(air)

    def on_touch(self, player: ObjectAPI) -> bool:
        from .. player import PlayerObject
        if isinstance(player, PlayerObject):
            player.client.force_query(self.query_instance(player))
        return True


class OxygenTankBaseItem(BaseItem):
    def __init__(self, identity: str):
        super().__init__(identity)
        self.air_block: Optional['Item'] = None
        self.pressure_multiplier = 25
        self._yield = 10

    def get_instance(self, x: int, y: int, team: 'Team', **kwargs) -> BaseInstance:
        return OxygenTankBaseInstance(x, y, self, team, **kwargs)

    def parse(self, v):
        from .. items import Item
        super().parse(v)
        if "yield" in v:
            self._yield = v["yield"]
        if "pressure_multiplier" in v:
            self.pressure_multiplier = v["pressure_multiplier"]
