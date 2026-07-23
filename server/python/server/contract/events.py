from typing import Dict
from .. items import Item
from .. bases import BaseItem


class ContractEvent(object):
    pass


class BasePlaced(ContractEvent):
    def __init__(self, base: BaseItem):
        self.base = base


class ItemDelivered(ContractEvent):
    def __init__(self, items: Dict[Item, int]):
        self.items = items
