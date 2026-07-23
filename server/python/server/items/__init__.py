from typing import List, Dict, Optional, Callable, Union, Tuple, TYPE_CHECKING
from enum import Enum

from .. import loc

if TYPE_CHECKING:
    from .. api.object import ObjectAPI
    from .. inventory import InventoryRecord
    from .. crafting import CraftingRecipy, CraftingRecipes
    from .. tuning import StoreItem
    from .. team import Team


class PlacingError(Exception):
    def __init__(self, message):
        self.message = message


class ItemRemoveState(Enum):
    PICKING = "picking"


class Item(object):
    ITEMS: Dict[str, 'Item'] = {}

    def __init__(self, identity: str, name: str = "", description: str = "", icon: int = 0,
                 stack_limit: int = 1,
                 place_checks: list = None, remove_checks: list = None,
                 placer: Callable[['Item', 'ObjectAPI', int, int], Optional['Item']] = None,
                 remover: Callable[['Item'], 'Item'] = None,
                 time_to_remove: [['ObjectAPI'], int] = None):
        Item.ITEMS[identity] = self
        self.identity = identity
        self.name = name
        self.description = description
        self.documentation = None
        self.actions_documentation = None
        self.icon = icon
        self.stack_limit = stack_limit
        self.store_item: Optional[StoreItem] = None
        self.place_checks = place_checks or []
        self.place_denied = False
        self.remove_checks = remove_checks or []
        self.placer = placer or place_as_is
        self.remover = remover or remove_as_is
        self.time_to_remove = time_to_remove or zero_time_to_remove
        self.self_action_title: Optional[bytes] = None
        self.self_action: Optional[Callable[[ObjectAPI], bool]] = None
        self.hit_animations = None
        self.remove_state: Optional[ItemRemoveState] = None
        self.weapon = False
        self.selectable = True
        self.hit_distance = 0
        self.hit_consumable: Optional['Item'] = None
        self.hit_auto = 0
        self.hit_delay = 10
        self.hit_multiple = 1
        self.hit_range = 0
        self.hit_sound = None
        self.hit_effect = None
        self.degradation_per_hit = 0
        self.hit_value = 0
        self.hit_offset = 0
        self.hold_object_state = 0
        self.hit_capacity = 0
        self.hit_refill_item: Optional['Item'] = None
        self.hit_refill_amount = 0
        self.hit_refill_time = 0
        self.unlock_contract = None
        self.burn_time = 0
        self.power_source = 0
        self.credits = 0
        self.worm_trigger = 0  # Points added to worm digging score when this block is removed
        self.attrs = {}

    def pure_icon(self):
        return self.icon & 0xFF

    def simple_item(self) -> bool:
        return self.identity != ""

    def get_crafting_recipies(self) -> List['CraftingRecipy']:
        from .. crafting import CraftingRecipes
        return [
            x
            for x in CraftingRecipes.ALL
            if x.item == self
        ]

    def is_locked(self, team: 'Team'):
        if self.unlock_contract:
            if self.unlock_contract not in team.completed_contracts:
                return True
        return False

    def get_store_pricing(self) -> Optional['StoreItem']:
        from .. tuning import Tuning
        return Tuning.PRICING.get(self, None)

    def parse(self, v):
        from .. blockspawn import BlockSpawn
        from .. crafting import CraftingRecipes
        from .. tuning import Tuning
        from .. contract.list import CONTRACTS_BY_ID

        if "name" in v:
            self.name = v["name"]
        if "description" in v:
            self.description = v["description"]
        if "documentation" in v:
            self.documentation = v["documentation"]
        if "actions" in v:
            self.actions_documentation = v["actions"]
        if "icon" in v:
            self.icon = v["icon"]
        if "stack_limit" in v:
            self.stack_limit = v["stack_limit"]
        if "selectable" in v:
            self.selectable = bool(v["selectable"])
        if "burn_time" in v:
            self.set_burn_time(v["burn_time"])
        if "power_source" in v:
            self.set_power_source(v["power_source"])
        if "credits" in v:
            self.credits = int(v["credits"])
        if "worm_trigger" in v:
            self.worm_trigger = int(v["worm_trigger"])
        if "time_to_remove" in v:
            ttr = v["time_to_remove"]
            if "const" in ttr:
                self.set_time_to_remove(const_time_to_remove(ttr["const"]))
        if "place_checks" in v:
            pc = v["place_checks"]
            l = []
            if "const" in pc:
                l.append(const_failure(pc["const"]))
            if "deny" in pc:
                self.place_denied = True
                self.selectable = False
                l.append(deny_failure())
            self.set_place_checks(l)
        if "remove_checks" in v:
            rc = v["remove_checks"]
            l = []
            if "const" in rc:
                l.append(const_failure(rc["const"]))
            if "attr_required" in rc:
                ar = rc["attr_required"]
                l.append(attr_required(ar["attr"], ar["value"], ar["error"]))
            self.set_remove_checks(l)
        if "remove_swap_to" in v:
            self.remover = remove_swap_to(Item.ITEMS[v["remove_swap_to"]])
        if "weapon" in v:
            self.weapon = bool(v["weapon"])
        if "hold_object_state" in v:
            self.hold_object_state = v["hold_object_state"]
        if "hit_distance" in v:
            self.hit_distance = int(v["hit_distance"])
        if "hit_consumable" in v:
            self.hit_consumable = Item.ITEMS[v["hit_consumable"]]
        if "degradation_per_hit" in v:
            self.degradation_per_hit = v["degradation_per_hit"]
        if "hit_features" in v:
            hf = v["hit_features"]
            if ("left" in hf) and ("right" in hf):
                self.set_hit_features(hf["left"].encode(), hf["right"].encode())
            self.hit_value = hf["hit_value"]
            if "hit_offset" in hf:
                self.hit_offset = int(hf["hit_offset"])
            if "hit_auto" in hf:
                self.hit_auto = int(hf["hit_auto"])
            if "hit_delay" in hf:
                self.hit_delay = int(hf["hit_delay"])
            if "hit_amount" in hf:
                self.hit_multiple = int(hf["hit_amount"])
            if "hit_range" in hf:
                self.hit_range = int(hf["hit_range"])
            if "hit_sound" in hf:
                self.hit_sound = int(hf["hit_sound"])
            if "hit_effect" in hf:
                e = hf["hit_effect"]
                if isinstance(e, list):
                    self.hit_effect = [ee.encode() for ee in e]
                else:
                    self.hit_effect = hf["hit_effect"].encode()
            if "hit_capacity" in hf:
                self.hit_capacity = int(hf["hit_capacity"])
            if "hit_refill_item" in hf:
                self.hit_refill_item = Item.ITEMS[hf["hit_refill_item"]]
            if "hit_refill_amount" in hf:
                self.hit_refill_amount = int(hf["hit_refill_amount"])
            if "hit_refill_time" in hf:
                self.hit_refill_time = int(hf["hit_refill_time"])
        if "remove_state" in v:
            self.remove_state = ItemRemoveState(v["remove_state"])
        if "self_action" in v:
            sa = v["self_action"]
            actions: Dict[str, Callable] = {
                "heal_self": heal_self,
                "heat_self": heat_self,
            }
            self.set_self_action(sa["name"].encode(), actions[sa["action"]](**sa))
        if "attrs" in v:
            self.attrs = v["attrs"]
        if "create_block" in v:
            cr = v["create_block"]
            BlockSpawn.register(self, cr)
        if "crafting" in v:
            CraftingRecipes.parse(v["crafting"], self)
        if "store" in v:
            self.store_item = Tuning.parse(v["store"], self)
        if "unlock" in v:
            unlock = v["unlock"]
            if "contract" in unlock:
                contract_name = unlock["contract"]
                if contract_name in CONTRACTS_BY_ID:
                    self.unlock_contract = CONTRACTS_BY_ID[contract_name]
                else:
                    print("Warning: no such contract: {0}".format(contract_name))

    def __repr__(self) -> str:
        return "Item <{0}>".format(self.identity)

    def set_time_to_remove(self, time_to_remove: [['ObjectAPI'], int]) -> 'Item':
        self.time_to_remove = time_to_remove
        return self

    def is_nothing(self):
        return self == NOTHING

    def set_place_checks(self, place_checks: list) -> 'Item':
        self.place_checks = place_checks
        return self

    def set_remove_checks(self, remove_checks: list) -> 'Item':
        self.remove_checks = remove_checks
        return self

    def set_placer(self, placer: Callable[['Item', 'ObjectAPI', int, int], Optional['Item']]) -> 'Item':
        self.placer = placer
        return self

    def set_remover(self, placer: Callable[['Item'], 'Item']) -> 'Item':
        self.remover = placer
        return self

    def place(self, p: 'ObjectAPI', x: int, y: int) -> 'Item':
        for c in self.place_checks:
            failed = c(p)
            if failed is False:
                raise PlacingError(loc.YOU_CANNOT_PLACE_X.format(self.name))
            if isinstance(failed, str):
                raise PlacingError(failed)
        return self.placer(self, p, x, y)

    def on_touch(self, p: 'ObjectAPI', x: int, y: int) -> bool:
        return False

    def is_weapon(self):
        return self.weapon

    def set_self_action(self, title: bytes, cb: Callable[['ObjectAPI'], bool]) -> 'Item':
        self.self_action_title = title
        self.self_action = cb
        return self

    def set_burn_time(self, time: int) -> 'Item':
        self.burn_time = time
        return self

    def set_power_source(self, power_source: int) -> 'Item':
        self.power_source = power_source
        return self

    def set_hit_features(self, hit_left: bytes, hit_right: bytes) -> 'Item':
        self.hit_animations = [hit_left, hit_right]
        return self

    def remove(self, p: 'ObjectAPI') -> Tuple['Item', int, Optional['InventoryRecord']]:
        relevant_record = None
        for c in self.remove_checks:
            failed = c(p)
            if isinstance(failed, str):
                raise PlacingError(failed)
            if failed is not None:
                relevant_record = failed
        return self.remover(self), self.time_to_remove(p), relevant_record

    def remove_immediately(self, p: 'ObjectAPI') -> 'Item':
        return self.remover(self)


def const_failure(text: str):
    def check(p: 'ObjectAPI'):
        return text
    return check


def deny_failure():
    def check(p: 'ObjectAPI'):
        return False
    return check


def attr_required(attr: str, value: Union[int, str], error: str):
    def check(p: 'ObjectAPI'):
        for v in p.get_team().inventory.entries.values():
            if attr not in v.item.attrs:
                continue
            vv = v.item.attrs[attr]
            if isinstance(value, int):
                if vv >= value:
                    return v
            if vv == value:
                return v
        return error
    return check


def place_as_is(item: Item, p: 'ObjectAPI', x: int, y: int) -> Item:
    return item


def remove_as_is(item: Item) -> Item:
    return item


def remove_to_nothing(item: Item) -> Item:
    return NOTHING


def remove_swap_to(item: Item) -> Callable[[Item], Item]:
    def swap(ignore: Item):
        return item
    return swap


def const_time_to_remove(time: int) -> Callable[['ObjectAPI'], int]:
    def ret_time(o: 'ObjectAPI'):
        return time
    return ret_time


def heal_self(heal_amount: int, **kwargs) -> Callable[['ObjectAPI'], bool]:
    def heal(pl: 'ObjectAPI') -> bool:
        return pl.heal(heal_amount)
    return heal


def heat_self(heat_duration: int, **kwargs) -> Callable[['ObjectAPI'], bool]:
    def heat(pl: 'ObjectAPI') -> bool:
        return pl.apply_heat(heat_duration)
    return heat


def zero_time_to_remove(o: 'ObjectAPI') -> int:
    return 0


def depending_on_tools(tbl: dict, default: int) -> Callable[['ObjectAPI'], int]:
    def ret_time(o: 'ObjectAPI'):
        for tool, time in tbl.items():
            if o.get_team().inventory.has_item(tool):
                return time
        return default
    return ret_time


def parse_items():
    from . parsing import parse_items
    parse_items()


def get(item_id: str) -> 'Item':
    return Item.ITEMS[item_id]


def get_item(item_id: str) -> 'Item':
    return Item.ITEMS[item_id]


def register_items():
    from .. blockspawn import BlockSpawn
    from .. api.block import BlockObject
    BlockSpawn.register(NOTHING, lambda **kwargs: BlockObject(0))


NOTHING = Item("")

register_items()
parse_items()
