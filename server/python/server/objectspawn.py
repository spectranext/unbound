
from . api.map import MapAPI
from . api.object import ObjectAPI
from . bot.slime import Slime
from . bot.spider import Spider
from . bot.bot import Bot
from . bot.ship import Ship
from . objects.rocket import RocketObject

OBJECT_SPAWNERS = {
    Slime.SLIME: lambda server_map: Slime(20, 10, ObjectAPI.DATA_ENTRY_SLIME, ObjectAPI.DATA_ENTRY_SLIME_MOVING),
    Slime.SPIDER: lambda server_map: Spider(20, 100, ObjectAPI.DATA_ENTRY_SPIDER, ObjectAPI.DATA_ENTRY_SPIDER_MOVING),
    Bot.BOT: lambda server_map: Bot(20, 10, ObjectAPI.DATA_ENTRY_BOT, ObjectAPI.DATA_ENTRY_BOT_MOVING),
    Ship.SHIP1: lambda server_map: Ship(Ship.SHIP1, ObjectAPI.DATA_ENTRY_SHIP1, ObjectAPI.DATA_ENTRY_SHIP1),
    RocketObject.ROCKET: lambda server_map: RocketObject()
}


def allocate_object(server_map: MapAPI, kind: bytes):
    if kind not in OBJECT_SPAWNERS:
        return None
    return OBJECT_SPAWNERS[kind](server_map)
