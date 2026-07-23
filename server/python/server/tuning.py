from typing import Dict
from . import items


class StoreItem(object):
    def __init__(self, amount: int, value: int, allow_buyback: bool = True):
        self.amount = int(amount)
        self.value = int(value)
        self.allow_buyback = allow_buyback

    def purchasable(self) -> bool:
        return self.amount > 0


class Tuning(object):
    INITIAL_CREDITS = 200
    INITIAL_DEBT = 10000
    DEBT_WEEKLY_RATE = 0.10
    DEBT_CEILING = 100000
    SHIP_COST = 25
    SLIME_COUNT = 10
    SLIME_NIGHT_ONLY = True

    PRICING: Dict[items.Item, StoreItem] = {}

    @staticmethod
    def parse(it, item: items.Item) -> StoreItem:
        if "value" not in it:
            raise ValueError("Item {0}: store requires 'value' (0-100)".format(item.identity))
        amount = int(it.get("amount", 0))
        value = int(it["value"])
        if value < 0 or value > 100:
            raise ValueError("Item {0}: store value must be 0..100".format(item.identity))
        allow_buyback = bool(it.get("buyback", True))
        si = StoreItem(amount, value, allow_buyback)
        Tuning.PRICING[item] = si
        return si
