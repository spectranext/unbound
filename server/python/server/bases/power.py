from typing import Set, Iterable, Optional, TYPE_CHECKING, Dict, Generator

import math

from . import BaseItem, BlockAssignment, BaseInstance
from . inventory import InventoryQueryResponse, PlacingInventoryFilter
from .. api.query import QueryResponse, OPT, NOACT
from .. api.map import MapAPI
from .. api.device import DeviceAPI, DeviceAPIHandler, DeviceAPISession, YieldAndClose
from .. api.object import ObjectAPI
from .. import blocks
from .. inventory import Inventory
from . device import Device
from .. import loc


if TYPE_CHECKING:
    from .. items import Item
    from .. team import Team


class PowerNetworks(object):
    DISTANCE = 16

    @staticmethod
    def calculate_networks(bases: Iterable[BaseInstance]):
        power_entities: Iterable[PowerEntity] = [
            b for b in bases if isinstance(b, PowerEntity)
        ]

        for b in power_entities:
            b.reset_power_network()

        for b1 in power_entities:
            if b1.has_power_net():
                b1_net = b1.get_power_net()
                for b2 in power_entities:
                    if b2.belongs_to_net(b1_net):
                        continue
                    if math.hypot(b2.get_x() - b1.get_x(), b2.get_y() - b1.get_y()) > PowerNetworks.DISTANCE:
                        continue
                    if b2.has_power_net():
                        b1_net.merge_from(b2.get_power_net())
                    else:
                        b1_net.register(b2)
            else:
                for b2 in power_entities:
                    if math.hypot(b2.get_x() - b1.get_x(), b2.get_y() - b1.get_y()) > PowerNetworks.DISTANCE:
                        continue
                    net = b2.get_power_net()
                    if net is None:
                        net = PowerNetwork()
                        net.register(b2)
                        net.register(b1)
                    else:
                        net.register(b2)


class PowerEntity(object):
    def __init__(self):
        self.power_net: Optional[PowerNetwork] = None

    def reset_power_network(self):
        if self.power_net is not None:
            self.power_net.reset()
        self.power_net = None

    def get_x(self) -> int:
        pass

    def get_y(self) -> int:
        pass

    def set_power_network(self, net: 'PowerNetwork'):
        self.power_net = net

    def has_power_net(self) -> bool:
        return self.power_net is not None

    def get_power_net(self):
        return self.power_net

    def belongs_to_net(self, net: 'PowerNetwork') -> bool:
        return self.power_net == net

    def get_power_equilibrium(self) -> float:
        if self.power_net is None:
            return 0
        return PowerEquilibrium.calculate(self.power_net)

    def has_power_producers(self) -> bool:
        if self.power_net is None:
            return False
        pr = self.power_net.get_producers()
        if not pr:
            return False
        for p in pr:
            if p.produces_power():
                return True
        return False

    def get_power_consumers(self):
        if self.power_net is None:
            return []
        return self.power_net.get_consumers()

    def get_power_producers(self):
        if self.power_net is None:
            return []
        return self.power_net.get_producers()


class PowerProducer(PowerEntity):
    def get_producer_power(self) -> int:
        return 0

    def produces_power(self) -> bool:
        return False


class PowerConsumer(PowerEntity):
    def get_consumer_power(self) -> int:
        return 0

    def consumes_power(self) -> bool:
        return False


class PowerNetwork(object):
    def __init__(self):
        self.entities: Set[PowerEntity] = set()
        self.producers: Set[PowerProducer] = set()
        self.consumers: Set[PowerConsumer] = set()

    def register(self, e: PowerEntity):
        e.set_power_network(self)
        self.entities.add(e)
        if isinstance(e, PowerProducer):
            self.producers.add(e)
        if isinstance(e, PowerConsumer):
            self.consumers.add(e)

    def merge_from(self, net: 'PowerNetwork'):
        for e in net.entities:
            e.set_power_network(self)
        self.entities.update(net.entities)
        if net.producers:
            self.producers.update(net.producers)
        if net.consumers:
            self.consumers.update(net.consumers)

    def reset(self):
        self.entities = None
        self.producers = None
        self.consumers = None

    def get_producers(self):
        return self.producers

    def get_consumers(self):
        return self.consumers


class PowerEquilibrium(object):
    @staticmethod
    def calculate(net: PowerNetwork) -> float:
        s = net.get_producers()
        c = net.get_consumers()
        if not s:
            return -1
        if not c:
            return 0
        total_producers: int = 0
        for p in s:
            if not p.produces_power():
                continue
            total_producers += p.get_producer_power()
        total_consumers: int = 0
        for c1 in c:
            if not c1.consumes_power():
                continue
            total_consumers += c1.get_consumer_power()
        if total_consumers > total_producers:
            return -1
        if total_producers == 0:
            return -1
        return 1. - (total_producers - total_consumers) / total_producers


class PowerBaseQueryResponse(QueryResponse):
    def __init__(self, p: ObjectAPI, b: 'PowerBaseInstance', inventory: Inventory):
        super().__init__(b"", loc.POWER.encode())
        self.b = b
        self.options = [
            OPT(loc.POWER_INVENTORY, NOACT(InventoryQueryResponse, loc.POWER_INVENTORY, p, inventory), 60)
        ]
        self.actions = [loc.OK.encode()]


class PowerBaseState(object):
    WORKING = "Working"
    NO_FUEL = "No Fuel"
    NO_CONSUMERS = "No Consumers"
    OVERLOAD = "Overload"


class PowerPlacingInventoryFilter(PlacingInventoryFilter):
    def name(self) -> str:
        return "fuel"

    def filter(self, item: 'Item') -> bool:
        return item.burn_time > 0


class PowerBaseInstance(BaseInstance, PowerProducer, Device, DeviceAPIHandler):
    BURNABLE_FILTER = PowerPlacingInventoryFilter()

    def __init__(self, x: int, y: int, prototype: 'PowerBaseItem', team: Optional['Team'], producer_power: int, **kwargs):
        BaseInstance.__init__(self, x, y, prototype, team, **kwargs)
        PowerEntity.__init__(self)
        Device.__init__(self, team.team_id if team else 0, b"power", listen=self)
        self.inventory = Inventory()
        self.burning = False
        self.fuel = 0
        self.burning_rate = 1
        self.state = PowerBaseState.NO_CONSUMERS
        self.producer_power = producer_power
        self.progress_block = prototype.progress_block

    def on_device_session(self, session: DeviceAPISession) -> Generator[bytes, bytes, None]:
        action: bytes = (yield)
        if action == b"status":
            yield YieldAndClose(self.state.encode())
        else:
            yield YieldAndClose(b"?")

    def on_destroy(self):
        super().on_destroy()
        self.device_destroy()

    def serialize(self) -> Dict[bytes, bytes]:
        d = super().serialize()
        self.dev_serialize(d)
        d[b"inv"] = self.inventory.serialize()
        d[b"fuel"] = int(self.fuel).to_bytes(4, "little")
        return d

    def deserialize(self, data: Dict[bytes, bytes]):
        super().deserialize(data)
        self.dev_deserialize(data)
        self.inventory.deserialize(data[b"inv"])
        self.fuel = int.from_bytes(data[b"fuel"], "little")

    def get_x(self) -> int:
        return self.x

    def get_y(self) -> int:
        return self.y

    def get_producer_power(self) -> int:
        return self.producer_power

    def produces_power(self) -> bool:
        return self.get_total_fuel_amount() > 0

    def query_instance(self, player: ObjectAPI) -> Optional[QueryResponse]:
        if self.burning:
            description = loc.POWER_WORKING.encode() % \
                          (int(self.get_power_equilibrium() * 100), self.get_total_fuel_time())
        else:
            description = loc.POWER_NOT_WORKING.format(self.state).encode()

        return InventoryQueryResponse(
            self.device.get_hostname().decode(), description, player, self.inventory,
            placing_filter=PowerBaseInstance.BURNABLE_FILTER)

    def set_status(self, w: str):
        if w == self.state:
            return
        self.state = w

        if w == PowerBaseState.NO_CONSUMERS:
            self.set_block_code(self.progress_block.x, self.progress_block.y, blocks.NO_DEMAND)
        else:
            self.set_block_code(self.progress_block.x, self.progress_block.y,
                                blocks.DRILLING if w == PowerBaseState.WORKING else blocks.NO_POWER)

    def get_total_fuel_time(self):
        return int(self.get_total_fuel_amount() / self.burning_rate)

    def get_total_fuel_amount(self) -> float:
        b = self.fuel
        for r in self.inventory.entries.values():
            b += r.item.burn_time * r.amount
        return b

    def pull_power(self):
        consumers = self.get_power_consumers()
        if not consumers:
            self.burning = False
            self.set_status(PowerBaseState.NO_CONSUMERS)
            # no one is consuming
            return

        for r in self.inventory.entries.values():
            if r.item.burn_time:
                self.burning = True
                self.fuel = r.item.burn_time
                self.inventory.remove_item(r.item, 1)
                self.set_status(PowerBaseState.WORKING)
                return

        self.burning = False
        self.set_status(PowerBaseState.NO_FUEL)

    def on_update(self):
        self.burning_rate = self.get_power_equilibrium()
        if self.burning_rate == 0:
            self.burning = False
            self.set_status(PowerBaseState.NO_CONSUMERS)
            return
        elif self.burning_rate < 0:
            self.burning = False
            self.burning_rate = 0
            self.set_status(PowerBaseState.OVERLOAD)
            return

        if self.burning:
            if self.fuel > 0:
                self.fuel -= self.burning_rate
            if self.fuel <= 0:
                self.fuel = 0
                self.pull_power()
        else:
            if self.fuel > 0:
                self.burning = True
                self.set_status(PowerBaseState.WORKING)
            else:
                self.pull_power()

    def can_accept_inlet_item(self, item: Optional['Item'], amount: int, health: float) -> bool:
        if item is None:
            return False
        return item.burn_time > 0

    def accept_inlet_item(self, item: 'Item', amount: int, health: float):
        self.inventory.add_item(item, amount, health)

    def on_touch(self, player: ObjectAPI) -> bool:
        from .. player import PlayerObject
        if isinstance(player, PlayerObject):
            player.client.force_query(self.query_instance(player))
        return True


class PowerBaseItem(BaseItem):
    def __init__(self, identity: str):
        super().__init__(identity)
        self.producer_power = 0
        self.progress_block = BlockAssignment()

    def parse(self, v):
        super().parse(v)
        self.progress_block.parse(v["progress_block"])
        self.producer_power = v["producer_power"]

    def get_instance(self, x: int, y: int, team: 'Team', **kwargs) -> BaseInstance:
        return PowerBaseInstance(x, y, self, team, self.producer_power, **kwargs)


class PoleQueryResponse(QueryResponse):
    def __init__(self, pi: 'PowerPoleInstance'):
        super().__init__(b"", loc.POWER_POLE.encode())
        if pi.has_power_producers():
            self.description = loc.POWER_POLE_PRESENT.encode() % (int(pi.get_power_equilibrium() * 100))
        else:
            self.description = loc.POWER_POLE_NOT_PRESENT.encode()
        self.actions = [loc.OK.encode()]


class PowerPoleInstance(BaseInstance, PowerEntity):
    def __init__(self, x: int, y: int, prototype: 'BaseItem', team: Optional['Team'], **kwargs):
        BaseInstance.__init__(self, x, y, prototype, team, **kwargs)
        PowerEntity.__init__(self)

    def on_update(self):
        super().on_update()
        self.set_energized(self.has_power_producers())

    def set_energized(self, e: bool):
        code = blocks.POLE_TOP_ENERGIZED if e else blocks.POLE_TOP
        b = self.get_block(0, 1)
        if b is None:
            return
        if b.code != code:
            b.code = code
            MapAPI.instance.update_block(b.x, b.y, True)

    def query_instance(self, player: ObjectAPI) -> Optional['QueryResponse']:
        return PoleQueryResponse(self)

    def get_x(self) -> int:
        return self.x

    def get_y(self) -> int:
        return self.y


class PowerPoleItem(BaseItem):
    def __init__(self, identity: str):
        super().__init__(identity)

    def get_instance(self, x: int, y: int, team: 'Team', **kwargs) -> BaseInstance:
        return PowerPoleInstance(x, y, self, team, **kwargs)
