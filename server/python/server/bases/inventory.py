from typing import Optional, TYPE_CHECKING, Dict, Generator, List, Union, Callable

from .. api.device import DeviceAPISession, YieldAndClose
from .. api.query import QueryResponse, NothingQueryResponse, QueryResponseOption, OPT
from .. api.object import ObjectAPI
from .. tube import TubeOutletBlockObject, TubeLoadedItem
from .. inventory import InventoryRecord, Inventory
from . device import Device, DeviceAPIHandler
from .. import blocks
from .. import loc


from . import BaseItem, BaseInstance, BaseBlockObject

if TYPE_CHECKING:
    from .. team import Team
    from .. items import Item
    from .. player import PlayerObject


class InventoryBaseInstance(BaseInstance):
    def __init__(self, x: int, y: int, prototype: 'BaseItem', team: Optional['Team'], inventory: Inventory = None, **kwargs):
        super().__init__(x, y, prototype, team, **kwargs)
        self.inventory = inventory or Inventory()

    def serialize(self) -> Dict[bytes, bytes]:
        d = super().serialize()
        d[b"inv"] = self.inventory.serialize()
        return d

    def deserialize(self, data: Dict[bytes, bytes]):
        super().deserialize(data)
        self.inventory.deserialize(data[b"inv"])

    def on_touch(self, player: ObjectAPI) -> bool:
        from .. player import PlayerObject
        if isinstance(player, PlayerObject):
            q = self.query_instance(player)
            if isinstance(q, NothingQueryResponse):
                return True
            player.client.force_query(q)
        return True

    def query_instance(self, player: ObjectAPI) -> Optional[QueryResponse]:
        return InventoryQueryResponse("Inventory", None, player, self.inventory)

    def on_destroy(self):
        super().on_destroy()
        self.inventory = None


class BlockInventoryEntry(QueryResponseOption):
    def __init__(self, record: InventoryRecord, take_mode: bool, offset: int, secondary: bool = False):
        self.record = record
        self.take_mode = take_mode
        self.offset = offset
        self._secondary = secondary

    def secondary(self) -> bool:
        return self._secondary

    def icon(self):
        return self.record.item.icon

    def __str__(self):
        if self.record.item.stack_limit == 1:
            str_health = str(int(100. * self.record.health)) + '%'
            return "{0}{1}{2}".format(
                self.record.item.name,
                " " * (28 - len(self.record.item.name) - len(str_health)), str_health)

        str_amount = str(self.record.amount)
        return "{0}{1}{2}".format(
            self.record.item.name,
            " " * (28 - len(self.record.item.name) - len(str_amount)), str_amount)


class TakeAllInventoryEntry(QueryResponseOption):
    def __init__(self, name: str, take_mode: bool, secondary: bool = False):
        self.name = name
        self.take_mode = take_mode
        self._secondary = secondary

    def secondary(self) -> bool:
        return self._secondary

    def __bytes__(self):
        return "{0}".format(self.name).encode()


class PlacingInventoryFilter(object):
    def name(self) -> str:
        return ""

    def filter(self, item: 'Item') -> bool:
        return False


class InventoryQueryResponse(QueryResponse):
    def __init__(self, name: str, description: Optional[bytes], player: ObjectAPI,
                 inventory: Inventory, take_only: bool = False, selected_option: int = 0,
                 placing_filter: Optional[PlacingInventoryFilter] = None):
        super().__init__(
            b"block_inventory", name.encode())

        from .. player import PlayerObject

        self.take_only = take_only
        self.player = player
        self.name = name
        self.description = description
        self.inventory = inventory
        self.placing_filter = placing_filter

        self.actions = [loc.MOVE_1.encode(), loc.MOVE_ALL.encode()]

        self.options = []
        self.current = selected_option
        offset = 1
        zero_current = selected_option == 0

        if self.inventory.entries:
            self.options.append(
                TakeAllInventoryEntry("{0} [{1}]".format(name, loc.INVENTORY_TAKE_ALL),
                                      True, secondary=True))
        else:
            self.options.append(
                TakeAllInventoryEntry("{0} [{1}]".format(name, loc.INVENTORY_EMPTY),
                                      True, secondary=True))

        for v in self.inventory.entries.values():
            self.options.append(
                BlockInventoryEntry(v, True, offset, secondary=True))
            if zero_current:
                self.current = offset
            offset += 1

        if (not take_only) and isinstance(player, PlayerObject):
            if self.player_has_items(player):
                if self.placing_filter:
                    self.options.append(TakeAllInventoryEntry(
                        "{1} [{2}, {0}]".format(self.placing_filter.name(),
                                                  loc.INVENTORY_BASE, loc.INVENTORY_PUT_ALL), False,
                    secondary=False))
                else:
                    self.options.append(TakeAllInventoryEntry("=={0} [{1}]".format(
                        loc.INVENTORY_BASE, loc.INVENTORY_PUT_ALL), False,
                        secondary=False))
            else:
                if self.placing_filter:
                    self.options.append(TakeAllInventoryEntry(
                        "{1} [{2}, {0}]".format(self.placing_filter.name(),
                                                  loc.INVENTORY_BASE, loc.INVENTORY_NOTHING), False,
                        secondary=False))
                else:
                    self.options.append(TakeAllInventoryEntry("=={0} [{1}]".format(
                        loc.INVENTORY_BASE, loc.INVENTORY_NOTHING), False,
                        secondary=False))
            offset += 1
            for v in player.get_team().inventory.entries.values():
                if self.placing_filter and (not self.placing_filter.filter(v.item)):
                    continue
                self.options.append(BlockInventoryEntry(v, False, offset, secondary=False))
                if zero_current:
                    self.current = offset
                offset += 1

        if offset == 0:
            self.options.append(OPT("<< {0} >>".format(loc.INVENTORY_NOTHING)))

    def player_has_items(self, player: 'PlayerObject') -> bool:
        for v in player.get_team().inventory.entries.values():
            if self.placing_filter and (not self.placing_filter.filter(v.item)):
                continue
            return True
        return False

    def new_instance(self, offset: int):
        return InventoryQueryResponse(
            self.name, self.description, self.player, self.inventory,
            selected_option=offset, placing_filter=self.placing_filter)

    def selected(self, option: int, action: bytes) -> Optional['QueryResponse']:
        from .. player import PlayerObject

        if not isinstance(self.player, PlayerObject):
            return None
        if option < 0 or option >= len(self.options):
            return None

        o = self.options[option]

        if isinstance(o, TakeAllInventoryEntry):
            if o.take_mode:
                for bie in list(self.inventory.entries.values()):
                    self.player.add_to_inventory(bie.item, bie.amount, bie.health)
                    del self.inventory.entries[bie.record_id]
                if self.auto_destroy():
                    return None
                if self.take_only:
                    return None
            else:
                for bie in list(self.player.get_team().inventory.entries.values()):
                    if self.placing_filter and (not self.placing_filter.filter(bie.item)):
                        continue
                    self.inventory.add_item(bie.item, bie.amount, bie.health)
                    del self.player.get_team().inventory.entries[bie.record_id]

            return self.new_instance(0)

        if not isinstance(o, BlockInventoryEntry):
            return self.new_instance(0)

        if o.take_mode:
            if option < 0 or option >= len(self.options):
                return None
            bie = self.options[option].record

            if (bie.amount > 1) and (action == loc.MOVE_1.encode()):
                self.player.add_to_inventory(bie.item, 1, bie.health)
                self.inventory.remove_item(bie.item, 1)

                return self.new_instance(option)
            else:
                self.player.add_to_inventory(bie.item, bie.amount, bie.health)
                del self.inventory.entries[bie.record_id]

            if self.inventory.has_something():
                return self.new_instance(0)
            else:
                if self.auto_destroy():
                    return None
                return self.new_instance(0)
        else:
            if option < 0 or option >= len(self.options):
                return None
            bie = self.options[option].record

            if (bie.amount > 1) and (action == loc.MOVE_1.encode()):
                self.inventory.add_item(bie.item, 1, bie.health)
                self.player.get_team().inventory.remove_item(bie.item, 1)
                return self.new_instance(option)
            else:
                self.inventory.add_item(bie.item, bie.amount, bie.health)
                del self.player.get_team().inventory.entries[bie.record_id]
                return self.new_instance(0)

    def can_place(self):
        return True

    def auto_destroy(self):
        return False


class TakeOnlyInventoryQueryResponse(InventoryQueryResponse):
    def __init__(self, name: str, description: Optional[bytes], player: ObjectAPI,
                 inventory: Inventory, base: InventoryBaseInstance = None, selected_option: int = 0, auto_destroy: bool = True):
        super().__init__(name, description, player, inventory, True, selected_option=selected_option)
        self.base = base
        self._auto_destroy = auto_destroy

    def can_place(self):
        return False

    def new_instance(self, offset: int):
        return TakeOnlyInventoryQueryResponse(
            self.name, self.description, self.player, self.inventory, self.base, selected_option=offset)

    def auto_destroy(self):
        if not self._auto_destroy:
            return False
        if self.base:
            self.base.destroy()
        return True


class InventoryBoxItem(BaseItem):
    class InventoryBoxBaseInstance(InventoryBaseInstance):
        def __init__(self, x: int, y: int, prototype: 'BaseItem', team: Optional['Team'],
                     inventory: Inventory = None, quick_pickup = False, **kwargs):
            super().__init__(x, y, prototype, team, inventory=inventory, **kwargs)
            self.quick_pickup = quick_pickup

        def do_quick_pickup(self, player: ObjectAPI):
            for bie in list(self.inventory.entries.values()):
                player.add_to_inventory(bie.item, bie.amount, bie.health)
                del self.inventory.entries[bie.record_id]
            self.destroy()

        def query_instance(self, player: ObjectAPI) -> Optional[QueryResponse]:
            name = loc.CHEST_INVENTORY
            if len(self.inventory.entries.values()) == 1:
                for v in self.inventory.entries.values():
                    if self.quick_pickup:
                        # do not do any menus, just pickup
                        self.do_quick_pickup(player)
                        return NothingQueryResponse()
                    name = v.item.name
                    break
            return TakeOnlyInventoryQueryResponse(
                name, None, player, self.inventory, self)

        def on_touch(self, player: ObjectAPI) -> bool:
            from .. player import PlayerObject
            if isinstance(player, PlayerObject):
                q = self.query_instance(player)
                if isinstance(q, NothingQueryResponse):
                    return True
                player.client.force_query(q)
            return True

    def __init__(self, identity: str):
        super().__init__(identity, [blocks.CRATE], 1, 1)

    def get_blocks(self, instance: BaseInstance) -> Optional[List[Union[int, Callable[['BaseItem', BaseInstance], BaseBlockObject]]]]:
        i: InventoryBoxItem.InventoryBoxBaseInstance = instance
        if len(i.inventory.entries.values()) == 1:
            for v in i.inventory.entries.values():
                return [v.item.icon]
        return super().get_blocks(instance)

    def get_instance(self, x: int, y: int, team: 'Team', **kwargs) -> BaseInstance:
        return InventoryBoxItem.InventoryBoxBaseInstance(x, y, self, team, **kwargs)


class CollectorBoxItem(BaseItem):
    class CollectorBoxBaseInstance(InventoryBaseInstance, Device, DeviceAPIHandler):
        def __init__(self, x: int, y: int, prototype: 'BaseItem', team: Optional['Team'], **kwargs):
            InventoryBaseInstance.__init__(self, x, y, prototype, team, **kwargs)
            Device.__init__(self, team.team_id if team else 0, b"collector", listen=self)

        def on_device_session(self, session: DeviceAPISession) -> Generator[bytes, bytes, None]:
            from .. items import Item

            action: bytes = (yield)
            if action == b"amount":
                tp_b: bytes = (yield)
                tp = tp_b.decode()
                if tp == "":
                    count = self.inventory.count_total_items()
                else:
                    if tp not in Item.ITEMS:
                        yield YieldAndClose(b"?")
                        return
                    item = Item.ITEMS[tp]
                    count = self.inventory.count_items(item)
                yield YieldAndClose(str(count).encode())
            if action == b"pull":
                tp_b: bytes = (yield)
                tp = tp_b.decode()
                if tp == "":
                    self.pull()
                else:
                    if tp in Item.ITEMS:
                        self.pull_item(Item.ITEMS[tp])

        def serialize(self) -> Dict[bytes, bytes]:
            d = super().serialize()
            self.dev_serialize(d)
            return d

        def deserialize(self, data: Dict[bytes, bytes]):
            super().deserialize(data)
            self.dev_deserialize(data)

        def pull_item(self, item: 'Item'):
            if not self.inventory.has_item(item):
                return
            health, remaining = self.inventory.remove_item(item, 1)
            out = self.get_outlet_plug()
            if out.loaded_item:
                return
            out.load(out, TubeLoadedItem(out, item, 1, health))

        def pull(self):
            for k, v in self.inventory.entries.items():
                self.pull_item(v.item)
                break

        def query_instance(self, player: ObjectAPI) -> Optional[QueryResponse]:
            return InventoryQueryResponse(
                loc.CHEST_INVENTORY, None, player, self.inventory, take_only=False)

        def can_accept_inlet_item(self, item: Optional['Item'], amount: int, health: float) -> bool:
            if item is None:
                return False
            return not self.inventory.is_full()

        def accept_inlet_item(self, item: 'Item', amount: int, health: float):
            self.inventory.add_item(item, amount, health)

        def get_outlet_plug(self) -> TubeOutletBlockObject:
            return self.get_block(0, 0)

        def on_touch(self, player: ObjectAPI) -> bool:
            from .. player import PlayerObject
            if isinstance(player, PlayerObject):
                player.client.force_query(self.query_instance(player))
            return True

    def __init__(self, identity: str):
        super().__init__(identity)

    def get_instance(self, x: int, y: int, team: 'Team', **kwargs) -> BaseInstance:
        return CollectorBoxItem.CollectorBoxBaseInstance(x, y, self, team, **kwargs)
