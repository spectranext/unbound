import functools
from typing import Optional, List, Tuple, Dict, Union

from ... api.query import QueryResponse, OPT, NOACT, QueryResponseOption
from ... bases.fob import FOBBaseInstance
from ... player import PlayerObject
from ... items import Item
from ... tuning import Tuning
from ... market import Market
from ... art import SHIP, CART
from ... import loc, icons


class ClientShopCart(object):
    def __init__(self):
        self.items: List[Tuple[Item, int]] = []

    def full_price(self) -> int:
        return Tuning.SHIP_COST + self.price()

    def price(self) -> int:
        s = 0
        m = Market.instance()
        for item, amount in self.items:
            it = Tuning.PRICING.get(item, None)
            if it is None or not it.purchasable():
                continue
            s += m.get_buy_price(item) * amount
        return s

    def clear(self):
        self.items = []

    def serialize(self) -> Dict[Item, int]:
        result = {}
        for it, am in self.items:
            si = Tuning.PRICING.get(it, None)
            if si is None or not si.purchasable():
                continue
            result[it] = result.get(it, 0) + si.amount * am
        return result

    def has_any(self) -> bool:
        return len(self.items) > 0

    def add_order(self, item: Item, amount: int):
        for index, itm in enumerate(self.items):
            if item == itm[0]:
                self.items[index] = (item, itm[1] + amount)
                return
        self.items.append((item, amount))

    def delete_order(self, item: Item):
        for index, itm in enumerate(self.items):
            iii, amount = itm
            if item == iii:
                self.items.pop(index)
                return


class ClearCart(QueryResponseOption):
    def __init__(self, base: FOBBaseInstance, p: PlayerObject):
        self.base = base
        self.p = p

    def __str__(self):
        return loc.STAR_STORE_CLEAR_CART

    def icon(self) -> Union[int, bytes]:
        return icons.ICON_CLEAR_CART

    def act(self, action: bytes) -> Optional['QueryResponse']:
        self.base.base_cart.clear()
        return get_star_store(self.base, self.p)


class ItemOnShop(QueryResponseOption):
    from ... items import Item

    def __init__(self, item: Item, base: FOBBaseInstance, p: PlayerObject, index: int = None):
        self.item = item
        self.base = base
        self.p = p
        self.index = index

    def secondary(self) -> bool:
        return True

    def icon(self):
        team = self.p.get_team()
        if team and self.item.is_locked(team):
            return icons.ICON_LOCKED
        return self.item.icon

    def act(self, action: bytes) -> Optional['QueryResponse']:
        team = self.p.get_team()
        if team and self.item.is_locked(team):
            return StarStoreItemLocked(self.base, self.p, self.item, self.index)
        self.base.base_cart.add_order(self.item, 1)
        return get_star_store(self.base, self.p, index=self.index)

    def __str__(self):
        si = self.item.get_store_pricing()
        price = Market.instance().get_buy_price(self.item)
        return "{0}c {1} x{2}".format(price, self.item.name, si.amount)

class ItemOnCart(QueryResponseOption):
    from ... items import Item

    def __init__(self, item: Item, amount: int, base: FOBBaseInstance, p: PlayerObject):
        self.item = item
        self.amount = amount
        self.base = base
        self.p = p

    def __str__(self):
        si = self.item.get_store_pricing()
        return "{0}x {1}".format(si.amount * self.amount, self.item.name)

    def icon(self):
        return self.item.icon

    def act(self, action: bytes) -> Optional['QueryResponse']:
        self.base.base_cart.delete_order(self.item)
        return get_star_store(self.base, self.p)


class StarStoreClosed(QueryResponse):
    def __init__(self, p: PlayerObject):
        super().__init__(b"", loc.STAR_STORE_CLOSED.encode())
        self.description = loc.STAR_STORE_CLOSED_DESC.encode()
        self.actions = [loc.OK]


def get_star_store(base: FOBBaseInstance, p: PlayerObject, index = None) -> QueryResponse:
    if p.get_team() and p.get_team().is_star_store_closed():
        return StarStoreClosed(p)
    return StarStore(base, p, index=index)


class StarStoreItemLocked(QueryResponse):
    def __init__(self, base: FOBBaseInstance, p: PlayerObject, item: Item, index=None):
        uc = item.unlock_contract
        cname = uc.name if uc else "?"
        super().__init__(b"", item.name.encode())
        self.description = loc.STAR_STORE_ITEM_LOCKED.format(cname).encode()
        self.actions = [loc.OK.encode()]
        self.image = icons.ICON_LOCKED
        self.base = base
        self.p = p
        self.index = index

    def selected(self, option: int, action: bytes) -> Optional[QueryResponse]:
        return get_star_store(self.base, self.p, index=self.index)

    def cancelled(self) -> Optional[QueryResponse]:
        return get_star_store(self.base, self.p, index=self.index)

class StarStoreHint(QueryResponse):
    def __init__(self, base: FOBBaseInstance, p: PlayerObject, title: str, description: str):
        super().__init__(b"", title.encode())
        self.description = description.encode()
        self.actions = [loc.OK.encode()]
        self.base = base
        self.p = p

    def selected(self, option: int, action: bytes) -> Optional['QueryResponse']:
        return StarStore(self.base, self.p)

    def cancelled(self) -> Optional['QueryResponse']:
        return StarStore(self.base, self.p)


class StarStore(QueryResponse):
    def __init__(self, base: FOBBaseInstance, p: PlayerObject, index=None):
        super().__init__(b"", loc.STAR_STORE_CART.encode())
        self.p = p
        self.base = base

        cart_price = self.base.base_cart.price()
        full_price = self.base.base_cart.full_price()
        credits = p.get_team().credits

        self.options = [
            OPT(loc.STAR_STORE_CART_HEADER,
                NOACT(StarStoreHint, base, p, loc.STAR_STORE_CART_HEADER, loc.STAR_STORE_CART_HEADER_DESC)),
            OPT(loc.STAR_STORE_FUNDS.format(credits),
                NOACT(StarStoreHint, base, p, loc.STAR_STORE_FUNDS.format(credits), loc.STAR_STORE_FUNDS_DESC),
                icon=icons.ICON_CREDITS),
            OPT(loc.STAR_STORE_SHIP_FEE.format(Tuning.SHIP_COST),
                NOACT(StarStoreHint, base, p, loc.STAR_STORE_SHIP_FEE.format(Tuning.SHIP_COST),
                      loc.STAR_STORE_SHIP_FEE_DESC),
                icon=icons.ICON_ORDERSHIP),
            OPT(loc.STAR_STORE_CART_TOTAL.format(cart_price),
                NOACT(StarStoreHint, base, p, loc.STAR_STORE_CART_TOTAL.format(cart_price),
                      loc.STAR_STORE_CART_TOTAL_DESC),
                icon=icons.ICON_CLEAR_CART),
            OPT(loc.STAR_STORE_ORDER_TOTAL.format(full_price),
                NOACT(StarStoreHint, base, p, loc.STAR_STORE_ORDER_TOTAL.format(full_price),
                      loc.STAR_STORE_ORDER_TOTAL_DESC),
                icon=icons.ICON_CREDITS),
        ]

        if full_price > credits:
            self.options.extend([
                OPT(loc.STAR_STORE_NOT_ENOUGH_TO_ORDER,
                    NOACT(StarStoreHint, base, p, loc.STAR_STORE_NOT_ENOUGH_TO_ORDER,
                          loc.STAR_STORE_NOT_ENOUGH_TO_ORDER_DESC),
                    icon=icons.ICON_CLEAR_CART)
            ])

        self.options.extend([
            ItemOnCart(item, amount, base, p)
            for item, amount in self.base.base_cart.items
        ])

        if self.base.base_cart.items:
            self.options.extend([
                ClearCart(base, p)
            ])

        if credits >= full_price:
            self.options.extend([
                OrderAShip(base, p)
            ])

        self.options.extend([
            OPT(loc.EXIT, icon=icons.ICON_EXIT)
        ])

        self.current = index if index is not None else len(self.options) + 1

        shop_lines = [(item, si) for item, si in Tuning.PRICING.items() if si.purchasable()]
        team = p.get_team()
        if team:
            shop_lines.sort(key=lambda pair: (pair[0].is_locked(team), pair[0].name.lower()))
        self.options.append(OPT(loc.STAR_STORE_ITEMS_HEADER,
                                NOACT(StarStoreHint, base, p, loc.STAR_STORE_ITEMS_HEADER,
                                      loc.STAR_STORE_ITEMS_HEADER_DESC), secondary=True))
        for item, si in shop_lines:
            self.options.append(ItemOnShop(item, base, p, index=len(self.options)))

        self.actions = [loc.OK.encode()]
        self.image = CART

class EmptyCartConfirmation(QueryResponse):
    def __init__(self, base: FOBBaseInstance, p: PlayerObject):
        super().__init__(b"", loc.STAR_STORE_CART_EMPTY.encode())
        self.base = base
        self.p = p
        self.description = loc.STAR_STORE_CART_EMPTY_DESC.encode()
        self.actions = [loc.STAR_STORE_OPEN.encode(), loc.STAR_STORE_ORDER_SHIP.encode()]

    def selected(self, option: int, action: bytes) -> Optional['QueryResponse']:
        if action == loc.STAR_STORE_OPEN.encode():
            return get_star_store(self.base, self.p)
        if action == loc.STAR_STORE_ORDER_SHIP.encode():
            self.base.order_ship()
            return None
        return None


class OrderAShip(QueryResponseOption):
    def __init__(self, base: FOBBaseInstance, p: PlayerObject):
        self.base = base
        self.p = p

    def icon(self) -> Union[int, bytes]:
        return icons.ICON_ORDERSHIP

    def __str__(self):
        return loc.STAR_STORE_ORDER_SHIP

    def act(self, action: bytes) -> Optional['QueryResponse']:
        from ... api.client import ClientAPI
        if self.base.ship_landed:
            self.p.client.queue_notify(loc.SHIP_ALREADY_LANDED.encode(), ClientAPI.NOTIFY_MESSAGE_COLOR_DANGER)
            return None
        if self.base.ship_landing:
            self.p.client.queue_notify(loc.SHIP_LANDING_PROGRESS.encode(), ClientAPI.NOTIFY_MESSAGE_COLOR_DANGER)
            return None
        need = self.base.base_cart.full_price()
        if self.p.client.get_team().credits < need:
            self.p.client.queue_notify(
                loc.STAR_STORE_NOT_ENOUGH_SUM.format(need).encode(),
                ClientAPI.NOTIFY_MESSAGE_COLOR_DANGER)
            return None
        if not self.base.base_cart.has_any():
            return EmptyCartConfirmation(self.base, self.p)
        self.base.order_ship()
        return None
