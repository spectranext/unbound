from typing import Callable, Dict

import yaml
import os

from . import get_item, Item
from . generation import Generation


def get_eval_binding():
    # needed for doing eval
    import random
    from .. import blocks
    from .. api.block import BlockObject, NothingBlockObject, NeighboringBlockObject
    from .. api.net import NetworkBlockObject
    from .. api.object import ObjectAPI
    from .. neighbors import Neighbors
    from .. tube import TubeBlockObject
    from .. liquid import LiquidBlockObject
    from .. ground import GroundBlockObject
    from .. hazards import PopstoneBlockObject, SpikeBlockObject
    from .. linkedblocks import LinkedBlockObject

    return {
        "BlockObject": BlockObject,
        "GroundBlockObject": GroundBlockObject,
        "LinkedBlockObject": LinkedBlockObject,
        "TubeBlockObject": TubeBlockObject,
        "Neighbors": Neighbors,
        "NetworkBlockObject": NetworkBlockObject,
        "NothingBlockObject": NothingBlockObject,
        "LiquidBlockObject": LiquidBlockObject,
        "PopstoneBlockObject": PopstoneBlockObject,
        "SpikeBlockObject": SpikeBlockObject,
        "NeighboringBlockObject": NeighboringBlockObject,
        "ObjectAPI": ObjectAPI,
        "blocks": blocks,
        "random": random,
        "get_item": get_item
    }


def get_items_binding():
    from .. bases import BaseSpawnerItem
    from .. bases.power import PowerBaseItem, PowerPoleItem
    from .. bases.factory import FactoryBaseItem
    from .. bases.fob import FOBBaseItem
    from .. bases.inventory import InventoryBoxItem, CollectorBoxItem
    from .. bases.computer import ComputerBaseItem
    from .. bases.router import RouterBaseItem
    from .. bases.door import DoorBaseItem
    from .. bases.oxygen import OxygenTankBaseItem
    from .. bases.spidernest import SpiderNestBaseItem
    from .. bases.light import LightBaseItem
    from . projectile import ProjectileItem

    return {
        "PowerBaseItem": PowerBaseItem,
        "BaseSpawnerItem": BaseSpawnerItem,
        "FactoryBaseItem": FactoryBaseItem,
        "RouterBaseItem": RouterBaseItem,
        "DoorBaseItem": DoorBaseItem,
        "FOBBaseItem": FOBBaseItem,
        "InventoryBoxItem": InventoryBoxItem,
        "CollectorBoxItem": CollectorBoxItem,
        "PowerPoleItem": PowerPoleItem,
        "OxygenTankBaseItem": OxygenTankBaseItem,
        "ComputerBaseItem": ComputerBaseItem,
        "SpiderNestBaseItem": SpiderNestBaseItem,
        "LightBaseItem": LightBaseItem,
        "ProjectileItem": ProjectileItem,
    }


def eval_constructor(loader, node):

    args = loader.construct_scalar(node)
    s = "lambda **kwargs: {0}".format(args)

    return eval(s, get_eval_binding())


def parse_items():
    yaml.add_constructor(u'!eval', eval_constructor)

    items_file_name = os.path.join(os.path.dirname(os.path.abspath(__file__)), "items.yaml")
    with open(items_file_name, "r") as f:
        document = yaml.load(f, yaml.Loader)

    items = document["items"]
    for item_id, v in items.items():
        if item_id not in Item.ITEMS:
            if "class" in v:
                cls = eval(v["class"], get_items_binding())
                cls(item_id)
            else:
                Item(item_id)

    items = document["items"]
    for item_id, v in items.items():
        if item_id not in Item.ITEMS:
            continue
        itm = get_item(item_id)
        print("Parsing {0}".format(str(itm)))
        itm.parse(v)

    superstructures = document["superstructures"]
    for super_id, v in superstructures.items():
        from .. map.superstructure import Superstructure

        print("Parsing superstructure {0}".format(str(super_id)))

        blocks = v["blocks"]
        data = list(filter(bool, str(v["data"]).split("\n")))
        width = len(data[0])
        height = len(data)
        wrapped = [
            [blocks[x] for x in line]
            for line in data
        ]
        Superstructure.SUPERSTRUCTURES[super_id] = Superstructure(super_id, wrapped, width, height)

    Generation.parse(document["generation"])

    from ..market import Market
    Market.bootstrap()
