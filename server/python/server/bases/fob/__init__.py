from typing import Optional, TYPE_CHECKING, Dict
import functools

from .. import BaseItem, BaseInstance
from ... api.map import MapAPI
from ... api.client import ClientAPI
from ... api.object import ObjectAPI
from ... api.query import QueryResponse, QueryResponseOption, OPT, NOACT
from ... import blocks
from .. power import PowerEntity
from ... contract.events import ItemDelivered
from ... tuning import Tuning
from ... market import Market
from ... inventory import Inventory
from ... import loc, icons

if TYPE_CHECKING:
    from ... team import Team
    from ... player import PlayerObject
    from ... bot.ship import Ship


def on_ship_returned(base: 'FOBBaseInstance', p: 'PlayerObject', ship: 'Ship'):
    received_items = ship.inventory.to_dict()
    base.team.contract_event(ItemDelivered(received_items))
    supplement = 0
    m = Market.instance()
    for it, amount in received_items.items():
        itm = Tuning.PRICING.get(it, None)
        if itm is None or not itm.allow_buyback:
            continue
        supplement += m.get_sell_price(it) * amount
    if base.team:
        base.team.add_credits(supplement)
    for it, amount in received_items.items():
        itm = Tuning.PRICING.get(it, None)
        if itm is None or not itm.allow_buyback:
            continue
        m.record_sell(it, amount)
    base.ship_leaving = False
    base.ship = None
    base.set_alert(False)
    if p:
        if supplement > 0:
            base.notify_nearby(
                loc.SHIP_RETURNED_CREDITS.format(supplement),
                color=ClientAPI.NOTIFY_MESSAGE_COLOR_REGULAR)
        else:
            base.notify_nearby(loc.SHIP_RETURNED, color=ClientAPI.NOTIFY_MESSAGE_COLOR_REGULAR)


class ReturnShip(QueryResponseOption):
    def __init__(self, base: 'FOBBaseInstance', p: 'PlayerObject'):
        self.base = base
        self.p = p

    def __str__(self) -> str:
        return loc.SHIP_RETURN_BACK

    def act(self, action: bytes) -> Optional[QueryResponse]:
        self.base.send_ship_back(self.p)
        return None


class FOBMenu(QueryResponse):
    def __init__(self, base: 'FOBBaseInstance', p: 'PlayerObject'):
        from . starstore import get_star_store, OrderAShip
        from ... bases.inventory import InventoryQueryResponse, TakeOnlyInventoryQueryResponse, PlacingInventoryFilter
        from ... query.inventory import InventoryQueryResponse as BaseInventoryQueryResponse

        class ValuablesPlacingInventoryFilter(PlacingInventoryFilter):
            def __init__(self, p: 'PlayerObject'):
                self.p = p

            def name(self) -> str:
                return loc.VALUABLES

            def filter(self, item: 'Item') -> bool:
                if item.get_store_pricing():
                    return True
                t = self.p.get_team()
                if t and t.contract_progress:
                    if t.contract_progress.is_item_relevant(item):
                        return True
                return False

        super().__init__(b"", "/{0}/ {1}".format(base.tag, loc.FOB_MENU).encode())
        self.options = [
        ]
        self.base = base
        if base.ship_landed:
            self.description = loc.SHIP_LANDED.format(base.leaving_in).encode()
            self.options.extend([
                ReturnShip(base, p)
            ])
        elif not base.ship_landing:
            self.options.extend([
                OPT(loc.STAR_STORE, NOACT(get_star_store, base, p), icons.ICON_STARSTORE),
                OrderAShip(base, p)
            ])
        self.options.extend([
            OPT(loc.BASE_STORAGE, NOACT(
                BaseInventoryQueryResponse, b"", p, base.base_storage, loc.BASE_STORAGE), icons.ICON_BASE_STORAGE),
            OPT(loc.SHIP_PENDING_PICKUP, NOACT(
                InventoryQueryResponse, loc.SHIP_PENDING_PICKUP, loc.SHIP_PENDING_PICKUP_DESC.encode(), p,
                base.pending_pickup, False, 0, ValuablesPlacingInventoryFilter(p)), icons.ICON_PENDING_PICKUP),
            OPT(loc.EXIT, icon=icons.ICON_EXIT)])
        self.actions = [b"OK"]
        self.p = p


class FOBBaseInstance(BaseInstance, PowerEntity):

    def __init__(self, x: int, y: int, prototype: 'BaseItem', team: 'Team', **kwargs):
        from ... bot.ship import Ship
        from . starstore import ClientShopCart

        super().__init__(x, y, prototype, team, **kwargs)
        PowerEntity.__init__(self)
        self.pending_pickup = Inventory()
        self.base_storage = team.inventory if team is not None else Inventory()
        self.ship_landing: bool = False
        self.ship_leaving: bool = False
        self.leaving_in = 0
        self.ship_cart: Optional[ClientShopCart] = None
        self.ship_landed: Optional[Ship] = None
        self.base_cart = ClientShopCart()

    def get_x(self) -> int:
        return self.x

    def get_y(self) -> int:
        return self.y

    def serialize(self) -> Dict[bytes, bytes]:
        d = super().serialize()
        d[b"pickup"] = self.pending_pickup.serialize()
        return d

    def deserialize(self, data: Dict[bytes, bytes]):
        super().deserialize(data)
        self.pending_pickup.deserialize(data[b"pickup"])

    def damage(self, v: int):
        for member in self.team.members:
            member.queue_notify(loc.FOB_UNDER_ATTACK.encode(), ClientAPI.NOTIFY_MESSAGE_COLOR_DANGER)
        super().damage(v)

    def set_leaving_countdown(self):
        self.leaving_in = 30

    def on_update(self):
        if self.ship_landed and self.leaving_in > 0:
            self.leaving_in -= 1
            if self.leaving_in <= 0:
                self.send_ship_back(None)

    def send_ship_back(self, p: Optional['PlayerObject']):
        if not self.ship_landed:
            return
        if self.ship_leaving:
            return

        self.ship_leaving = True

        def take_off(ship: 'Ship'):
            ship.inventory.move_items(self.pending_pickup)
            ship.set_on_fled(functools.partial(on_ship_returned, self, p, ship))
            ship.fly_off()

        MapAPI.instance.schedule_callback(functools.partial(take_off, self.ship_landed), 2000)
        self.ship_landed = None
        self.set_alert(True)
        self.notify_nearby(loc.FOB_TAKE_OFF_STAND_BY, color=ClientAPI.NOTIFY_MESSAGE_COLOR_REGULAR)

    def on_io_send(self, message: str):
        if message.upper() == "ORDERSHIP":
            self.order_ship()

    def on_io_request(self, prompt: str) -> str:
        if prompt.upper() == "STATUS":
            if self.ship_landed:
                return "LANDED"
            if self.ship_leaving:
                return "LEAVING"
            if self.ship_landing:
                return "LANDING"
            return "OK"
        return "0"

    def set_alert(self, alert: bool):
        self.set_block_code(0, 0, blocks.LAMP if alert else blocks.NO_LAMP)

    def query_instance(self, player: ObjectAPI) -> Optional[QueryResponse]:
        # noinspection PyTypeChecker
        return FOBMenu(self, player)

    def on_touch(self, player: ObjectAPI) -> bool:
        from ... player import PlayerObject
        if isinstance(player, PlayerObject):
            player.client.force_query(self.query_instance(player))
        return True

    def order_ship(self) -> bool:
        from ... api.client import ClientAPI
        from ... api.map import MapAPI
        from ... bot.ship import Ship
        from ... tuning import Tuning
        from . starstore import ClientShopCart

        price = self.base_cart.full_price()
        if self.team.credits < price:
            return False

        if self.ship_landing or self.ship_landed:
            return False

        self.team.remove_credits(price)
        for ord_item, mult in self.base_cart.items:
            si = Tuning.PRICING.get(ord_item)
            if si and si.purchasable():
                Market.instance().record_buy(ord_item, si.amount * mult)
        self.notify_nearby(loc.FOB_SHIP_ORDERED, color=ClientAPI.NOTIFY_MESSAGE_COLOR_BRIGHT)
        self.ship_landing = True
        self.set_alert(True)

        # move carts around
        self.ship_cart = self.base_cart
        self.base_cart = ClientShopCart()

        def on_landed(ship: Ship):
            self.base_storage.move_items(ship.inventory)
            self.notify_nearby(loc.FOB_SHIP_LANDED, color=ClientAPI.NOTIFY_MESSAGE_COLOR_BRIGHT)
            self.ship_landing = False
            self.ship_landed = ship
            self.set_leaving_countdown()
            self.set_alert(False)

        def landed(ship: Ship):
            MapAPI.instance.schedule_callback(functools.partial(on_landed, ship), 1000)

        def schedule_hit():
            # noinspection PyTypeChecker
            ship: Ship = MapAPI.instance.spawn_object(self.x + 1, 1, b"ship1")
            if self.ship_cart:
                for item, amount in self.ship_cart.serialize().items():
                    ship.inventory.add_item(item, amount, 1.)
                self.ship_cart = None

            ship.set_on_landed(functools.partial(landed, ship))

        MapAPI.instance.schedule_callback(schedule_hit, 5000)
        return True


class FOBBaseItem(BaseItem):
    def get_instance(self, x: int, y: int, team: 'Team', **kwargs) -> BaseInstance:
        return FOBBaseInstance(x, y, self, team, **kwargs)
