from typing import Optional, List, TYPE_CHECKING, Tuple, Union, Callable, Dict
import random
import math

from .. api.object import ObjectAPI
from .. api.client import ClientAPI
from .. api.map import MapAPI
from .. api.query import QueryResponse
from .. api.net import NetworkBlockInterface, BlockNetwork
from .. items import Item, NOTHING
from .. effects import MapEffects
from .. linkedblocks import LinkedBlockObject
from .. import blocks

if TYPE_CHECKING:
    from .. team import Team


class BaseInstance(object):
    def __init__(self, x: int, y: int, prototype: 'BaseItem', team: Optional['Team'], **kwargs):
        self.x = x
        self.y = y
        self.team = team
        self.neutral = False
        self.prototype = prototype
        self.link_id = MapAPI.instance.allocate_link()
        self.link: List['BaseBlockObject'] = MapAPI.instance.obtain_link(self.link_id)
        self.tag: str = ""
        self.health = prototype.health

        MapAPI.instance.bases.add_base(x, y, self)

    @staticmethod
    def parse_base(data: Dict[bytes, bytes]) -> Tuple[bytes, Optional[bytes], int, int]:
        team = data[b"tm"] if b"tm" in data else None
        x = int.from_bytes(data[b"x"], "little")
        y = int.from_bytes(data[b"y"], "little")
        return data[b"id"], team, x, y

    def serialize(self) -> Dict[bytes, bytes]:
        x = self.x.to_bytes(2, "little")
        y = self.y.to_bytes(2, "little")
        d = {
            b"id": self.prototype.identity.encode(),
            b"x": x,
            b"y": y,
        }
        if self.team:
            d[b"tm"] = self.team.name.encode()
        if self.tag:
            d[b"tag"] = self.tag.encode()
        return d

    def notify_nearby(self, message: str, color: int = ClientAPI.NOTIFY_MESSAGE_COLOR_DANGER, distance=32):
        from .. player import Client

        if not self.team:
            return
        for m in self.team.members:
            if not isinstance(m, Client):
                continue
            p = m.player
            if not p:
                continue
            if math.hypot(p.get_x() - self.x, p.get_y() - self.y) > distance:
                continue
            m.queue_notify(message.encode(), color)

        return None

    def damage(self, v: int):
        self.health -= v

        x = self.x
        y = self.y
        w = self.prototype.width
        h = self.prototype.height

        def blow_off():
            MapAPI.instance.send_effect(
                x + random.randint(-1, w - 1), y - random.randint(-1, h - 1), MapEffects.EXPLOSION)

        blow_off()

        if self.health <= 0:
            self.destroy()
            MapAPI.instance.schedule_callback(blow_off, 400)
            MapAPI.instance.schedule_callback(blow_off, 800)
            MapAPI.instance.schedule_callback(blow_off, 1100)

    def deserialize(self, data: Dict[bytes, bytes]):
        self.tag = data[b"tag"].decode() if b"tag" in data else MapAPI.instance.allocate_tag()

    def on_init(self):
        pass

    def on_touch(self, player: ObjectAPI) -> bool:
        return True

    def query_instance(self, player: ObjectAPI) -> Optional['QueryResponse']:
        return None

    def on_destroy(self):
        pass

    def on_update(self):
        pass

    def can_accept_inlet_item(self, item: Optional[Item], amount: int, health: float) -> bool:
        return False

    def accept_inlet_item(self, item: Item, amount: int, health: float):
        pass

    def get_block(self, x: int, y: int):
        return MapAPI.instance.get_block(self.x + x, self.y - y)

    def set_block_code(self, x: int, y: int, code: int):
        b = BaseBlockObject(code, self)
        b.set_item(self.prototype)
        MapAPI.instance.set_block(self.x + x, self.y - y, b, True)

    def destroy(self):
        from .. blockspawn import BlockSpawn

        for y_ in range(0, self.prototype.height):
            for x_ in range(0, self.prototype.width):
                MapAPI.instance.set_block(self.x + x_, self.y - y_, BlockSpawn.create_block(NOTHING), True)

        self.link = None
        MapAPI.instance.bases.remove_base(self.x, self.y)
        self.on_destroy()


class BaseBlockObject(LinkedBlockObject):
    def __init__(self, code: int = 0, base: BaseInstance = None, proto: Dict[bytes, bytes] = None, **kwargs):
        super().__init__(code=code, link=base.link_id, proto=proto, **kwargs)
        base.link.append(self)
        self.base = base

    def get_light(self) -> int:
        return blocks.MAXIMUM_LIGHT

    def pass_blocking_light(self, light: int) -> int:
        return light  # Base blocks are transparent to light

    def pass_light(self, light: int) -> int:
        return super().pass_light(light)

    def flushable(self):
        return False

    def serialize(self) -> Union[bool, Tuple[bytes, Dict[bytes, bytes]]]:
        # base block objects are not saved, bases contain all the context and completely recreate themselves
        return False

    def on_touch(self, player: ObjectAPI) -> bool:
        return self.base.on_touch(player)


class NetworkBaseBlockObject(BaseBlockObject, NetworkBlockInterface):
    def __init__(self, code: int = 0, base: BaseInstance = None, network_tag: str = "", classification: str = "",
                 proto: Dict[bytes, bytes] = None, **kwargs):
        BaseBlockObject.__init__(self, code=code, base=base, proto=proto, **kwargs)
        self.tag = network_tag
        self.classification = classification
        NetworkBlockInterface.__init__(self, BlockNetwork(network_tag, set()))

    def net_class(self) -> Optional[str]:
        return self.classification

    def get_location(self) -> Tuple[int, int]:
        return self.x, self.y

    def on_touch(self, player: ObjectAPI) -> bool:
        return self.base.on_touch(player)


def network_block_spawner(code: int, tag: str, classification: str):
    def spawn(bi: BaseInstance) -> BaseBlockObject:
        return NetworkBaseBlockObject(code, bi, tag, classification)
    return spawn


class BlockAssignment(object):
    def __init__(self):
        self.x = 0
        self.y = 0

    def parse(self, config: str):
        sp = config.split("x")
        self.x = int(sp[0])
        self.y = int(sp[1])


class BaseItem(Item):
    def __init__(self, identity: str, blocks:
                 Optional[List[Union[int, Callable[['BaseItem', BaseInstance], BaseBlockObject]]]] = None,
                 width: int = 0, height: int = 0):
        super().__init__(identity)
        self.width = width
        self.height = height
        self.blocks = blocks
        self.health = 100
        self.is_base = True
        self.tag = None

    def get_blocks(self, instance: BaseInstance) -> Optional[List[Union[int, Callable[['BaseItem', BaseInstance], BaseBlockObject]]]]:
        return self.blocks

    def simple_item(self) -> bool:
        return False

    def icons_set(self):
        bi = self.get_instance(0, 0, None)
        images = []
        row = []
        for b in self.blocks:
            if isinstance(b, int):
                code = b & 0xFF
            else:
                block = b(bi)
                code = block.code & 0xFF
            row.append(code)
            if len(row) >= self.width:
                images.append(row)
                row = []
        return reversed(images)

    def parse(self, v):
        # needed for eval below
        from .. tube import tube_inlet_spawner, tube_outlet_spawner
        from .. bases.router import router_spawner
        from .. import blocks

        scope = globals().copy()
        scope.update(locals())

        super().parse(v)
        if "width" in v and "height" in v and "blocks" in v:
            self.width = v["width"]
            self.height = v["height"]
            self.blocks = []
            _b = v["blocks"]
            for y in range(0, self.height):
                for x in range(0, self.width):
                    _id = "{0}x{1}".format(x, y)
                    self.blocks.append(eval(_b[_id], scope))
        if "health" in v:
            self.health = v["health"]
        if "tag" in v:
            self.tag = v["tag"]

    def get_instance(self, x: int, y: int, team: Optional['Team'], **kwargs) -> BaseInstance:
        return BaseInstance(x, y, self, team, **kwargs)


def spawn_base(base: BaseItem, x: int, y: int, team: Optional['Team'], **kwargs) -> BaseInstance:
    bi = base.get_instance(x, y, team, **kwargs)
    _i = 0
    bb = base.get_blocks(bi)
    for _y in range(0, base.height):
        for _x in range(0, base.width):
            block_at: Union[int, Callable[[BaseInstance], BaseBlockObject]] = bb[_i]
            if isinstance(block_at, int):
                b = BaseBlockObject(block_at, bi)
            else:
                b = block_at(bi)
            b.set_item(base)
            if base.tag:
                b.tag = base.tag
            _i += 1
            MapAPI.instance.set_block(x + _x, y - _y, b, True)
    MapAPI.instance.schedule_map_refresh(False)
    return bi


class BaseSpawnerItem(Item):
    def __init__(self, identity: str):
        self.base = None
        super().__init__(identity)
        self.is_spawner = True

    def simple_item(self) -> bool:
        return False

    def parse(self, v):
        from .. items import get_item
        super().parse(v)

        self.base = get_item(v["base"])
        self.placer = self._place_spawn_base_placer()

    def _place_spawn_base_placer(self):
        base_item = self.base

        def place(item: Item, p, x: int, y: int) -> Optional['Item']:
            from .. player import Client
            client: Client = p.client
            bi = spawn_base(base_item, x, y, client.get_team())
            bi.tag = MapAPI.instance.allocate_tag()
            client.base_installed(base_item, x, y)
            bi.on_init()
            return None
        return place
