from typing import Optional, Tuple, Union, TYPE_CHECKING, Dict

from . import BaseItem, BlockAssignment, BaseInstance, NetworkBaseBlockObject
from . inventory import InventoryQueryResponse, PlacingInventoryFilter
from . power import PowerConsumer
from .. crafting import CraftingRecipy, CraftingRecipes
from .. tube import TubeOutletBlockObject, TubeLoadedItem
from .. api.query import QueryResponseOption, QueryResponse, OPT, NOACT
from .. api.object import ObjectAPI
from .. import blocks
from .. import items
from .. inventory import Inventory
from .. import loc, icons


if TYPE_CHECKING:
    from .. team import Team


class ConfigureSelectedRecipy(QueryResponseOption):
    def __init__(self, b: 'FactoryBaseInstance', r: CraftingRecipy):
        self.b = b
        self.r = r

    def __str__(self) -> str:
        return "{0} of {1}".format(self.r.amount, self.r.item.name) if self.r.amount > 1 else self.r.item.name

    def icon(self):
        return self.r.item.icon

    def act(self, action: bytes) -> Optional['QueryResponse']:
        self.b.crafting_recipy = self.r
        return None


class ConfigureFactoryRecipyQueryResponse(QueryResponse):
    def __init__(self, p: ObjectAPI, b: 'FactoryBaseInstance'):
        super().__init__(b"", loc.FACTORY_CONFIG.encode())
        self.description = loc.FACTORY_CONFIG_DESC.encode()
        self.actions = [loc.SELECT.encode()]
        recipes = CraftingRecipes.suggest(p.get_team())
        self.options = [
            ConfigureSelectedRecipy(b, r)
            for r in recipes
        ]
        self.current = recipes.index(b.crafting_recipy) if b.crafting_recipy else 0
        self.p = p
        self.b = b


class FactoryBasePlacingInventoryFilter(PlacingInventoryFilter):
    def __init__(self, r: CraftingRecipy):
        self.r = r

    def name(self) -> str:
        return "For {0}".format(self.r.item.name)

    def filter(self, item: items.Item) -> bool:
        return item in self.r.consumables


class FactoryBaseQueryResponse(QueryResponse):
    def __init__(self, p: ObjectAPI, b: 'FactoryBaseInstance', inv_in: Inventory, inv_out: Inventory):
        super().__init__(b"", "/{0}/ {1}".format(b.tag, loc.FACTORY).encode())
        self.b = b
        if self.b.pressing > 0:
            self.description = loc.FACTORY_WORKING.encode()
        else:
            if b.state == FactoryBaseState.NO_WORK and b.missing_item:
                self.description = loc.FACTORY_NOT_ENOUGH_ITEM.format(b.missing_item.name).encode()
            else:
                self.description = loc.FACTORY_NOT_WORKING.format(b.state).encode()
        self.options = [
            OPT(loc.FACTORY_CRAFTING_OUT.format(b.crafting_recipy.amount, b.crafting_recipy.item.name)
                if b.crafting_recipy else loc.FACTORY_CONFIG,
                NOACT(ConfigureFactoryRecipyQueryResponse, p, b), icon=icons.ICON_CRAFTING),
        ]
        if b.crafting_recipy:
            inlet_filter = FactoryBasePlacingInventoryFilter(b.crafting_recipy)
            self.options.append(
                OPT(loc.FACTORY_SOURCE.format(b.in_inventory.count_total_items()),
                    NOACT(InventoryQueryResponse, loc.FACTORY_SOURCE,
                      loc.FACTORY_SOURCE.format(b.in_inventory.count_total_items()).encode(), p, inv_in,
                      placing_filter=inlet_filter), icon=icons.ICON_PENDING_PICKUP))
        self.options.append(
            OPT(loc.FACTORY_PRODUCED_ITEMS_TOTAL.format(inv_out.count_total_items()),
                NOACT(InventoryQueryResponse, loc.FACTORY_PRODUCED_ITEMS,
                      loc.FACTORY_PRODUCED_ITEMS.encode(), p, inv_out, take_only=True), icon=icons.ICON_DELIVERIES))
        self.actions = [loc.OK.encode()]


class FactoryBaseState(object):
    WORKING = "Working"
    NO_POWER = "No Power"
    NO_CONFIG = "No Config"
    NO_WORK = "No Work"
    CLOGGED = "Clogged"


class FactoryBaseInstance(BaseInstance, PowerConsumer):
    def __init__(self, x: int, y: int, prototype: 'FactoryBaseItem', team: Optional['Team'],
                 crafting_time: int = 5, power_consumption: int = 20, **kwargs):
        super().__init__(x, y, prototype, team, **kwargs)
        PowerConsumer.__init__(self)
        self.in_inventory = Inventory()
        self.out_inventory = Inventory()
        self.missing_item: Optional[items.Item] = None
        self.crafting_recipy: Optional[CraftingRecipy] = None
        self.pressing = 0
        self.crafting_time = crafting_time
        self.power_consumption = power_consumption
        self.state = FactoryBaseState.NO_WORK
        self.item_to_push: Optional[Tuple[items.Item, int, float]] = None
        self.progress_block = prototype.progress_block
        self.outlet_block = prototype.outlet_block
        self.clogged = False

    def get_x(self) -> int:
        return self.x

    def get_y(self) -> int:
        return self.y

    def serialize(self) -> Dict[bytes, bytes]:
        d = super().serialize()
        d[b"in"] = self.in_inventory.serialize()
        d[b"out"] = self.out_inventory.serialize()
        if self.crafting_recipy is not None:
            d[b"cr"] = self.crafting_recipy.item.identity.encode()
        return d

    def deserialize(self, data: Dict[bytes, bytes]):
        super().deserialize(data)
        if b"cr" in data:
            cr = data[b"cr"].decode()
            for recipy in CraftingRecipes.ALL:
                if recipy.item.identity == cr:
                    self.crafting_recipy = recipy
                    break
        else:
            self.crafting_recipy = None
        self.in_inventory.deserialize(data[b"in"])
        self.out_inventory.deserialize(data[b"out"])

    def get_consumer_power(self) -> int:
        return self.power_consumption

    def consumes_power(self) -> bool:
        return (self.pressing > 0) or (self.has_work() is True)

    def query_instance(self, player: ObjectAPI) -> Optional[QueryResponse]:
        return FactoryBaseQueryResponse(player, self, self.in_inventory, self.out_inventory)

    def set_status(self, w: str):
        if w == self.state:
            return
        self.state = w
        if w == FactoryBaseState.NO_WORK or w == FactoryBaseState.NO_CONFIG:
            self.set_block_code(self.progress_block.x, self.progress_block.y, blocks.NO_DEMAND)
        else:
            self.set_block_code(self.progress_block.x, self.progress_block.y,
                                blocks.PRESSING if w == FactoryBaseState.WORKING else blocks.NO_POWER)

    def get_outlet_plug(self) -> TubeOutletBlockObject:
        return self.get_block(self.outlet_block.x, self.outlet_block.y)

    def has_work(self) -> Union[bool, items.Item]:
        if self.crafting_recipy is None:
            return False
        return self.crafting_recipy.enough(self.in_inventory)

    def yield_work(self):
        itm = self.has_work()
        if isinstance(itm, items.Item):
            self.missing_item = itm
            self.set_status(FactoryBaseState.NO_WORK)
            return
        for k, v in self.crafting_recipy.consumables.items():
            self.in_inventory.remove_item(k, v)
        self.pressing = 5
        self.set_status(FactoryBaseState.WORKING)

    def do_push_outlet(self) -> bool:
        out = self.get_outlet_plug()
        item, amount, health = self.item_to_push
        if out.loadable(item, amount, health, True):
            self.clogged = False
            out.load(out, TubeLoadedItem(out, item, amount, health))
            return True
        else:
            self.clogged = True
            return False

    def push_outlet(self, item: items.Item, amount: int, health: float) -> bool:
        self.item_to_push = (item, amount, health)
        return self.do_push_outlet()

    def can_accept_inlet_item(self, item: Optional[items.Item], amount: int, health: float) -> bool:
        if item is None:
            return False
        if self.crafting_recipy is None:
            return False
        return self.crafting_recipy.matches_consumables(item)

    def accept_inlet_item(self, item: items.Item, amount: int, health: float):
        self.in_inventory.add_item(item, amount, health)

    def on_update(self):
        if self.out_inventory.has_something():
            for r in self.out_inventory.entries.values():
                amount_to_take = min(r.item.stack_limit, r.amount)
                if self.push_outlet(r.item, amount_to_take, r.health):
                    self.out_inventory.remove_item(r.item, amount_to_take)
                break
        if self.crafting_recipy is None:
            self.set_status(FactoryBaseState.NO_CONFIG)
            return
        if self.pressing <= 0:
            itm = self.has_work()
            if isinstance(itm, items.Item):
                self.missing_item = itm
                self.set_status(FactoryBaseState.NO_WORK)
                return
        if self.get_power_equilibrium() <= 0:
            self.set_status(FactoryBaseState.NO_POWER)
            return
        if self.pressing > 0:
            self.pressing -= 1
            if self.pressing <= 0:
                self.out_inventory.add_item(self.crafting_recipy.item, self.crafting_recipy.amount, 1.)
            return
        if self.out_inventory.is_full():
            if not self.do_push_outlet():
                self.set_status(FactoryBaseState.CLOGGED)
                return
        self.yield_work()

    def on_touch(self, player: ObjectAPI) -> bool:
        from .. player import PlayerObject
        if isinstance(player, PlayerObject):
            player.client.force_query(self.query_instance(player))
        return True


class FactoryBaseItem(BaseItem):
    def __init__(self, identity: str):
        super().__init__(identity)
        self.crafting_time = 5
        self.progress_block = BlockAssignment()
        self.outlet_block = BlockAssignment()
        self.power_consumption = 20

    def parse(self, v):
        super().parse(v)
        self.progress_block.parse(v["progress_block"])
        self.outlet_block.parse(v["outlet_block"])
        if "crafting_time" in v:
            self.crafting_time = v["crafting_time"]
        if "power_consumption" in v:
            self.power_consumption = v["power_consumption"]

    def get_instance(self, x: int, y: int, team: 'Team', **kwargs) -> BaseInstance:
        return FactoryBaseInstance(x, y, self, team, self.crafting_time, self.power_consumption, **kwargs)
