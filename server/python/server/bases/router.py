from typing import TYPE_CHECKING, Optional, Dict, Tuple, Callable, Set, List, Generator

from .. api.block import BlockObject
from .. api.map import MapAPI
from .. api.object import ObjectAPI
from .. api.query import QueryResponse, OPT
from .. api.device import DeviceAPISession, YieldAndClose
from . device import Device, DeviceQueryResponseOptions, DeviceAPIHandler
from .. blocks import ROUTER_UP, ROUTER_DOWN, ROUTER_LEFT, ROUTER_RIGHT
from .. blocks import SPLITTER_LEFT, SPLITTER_DOWN, SPLITTER_UP, SPLITTER_RIGHT
from . import BaseItem, BaseBlockObject, BaseInstance
from .. tube import TubeInterface, TubeLoadedItem
from .. items import Item
from .. import loc
from . computer import ComputerBaseInstance

if TYPE_CHECKING:
    from .. team import Team


class RouterConfiguration:
    ROUTER = "router"
    SPLITTER = "splitter"


def direction_to_offset(direction: str) -> Tuple[int, int]:
    if direction == "up":
        return 0, -1
    elif direction == "down":
        return 0, 1
    elif direction == "left":
        return -1, 0
    elif direction == "right":
        return 1, 0
    else:
        return 0, 0


class RouterSetTargetComputerQueryResponse(QueryResponse):
    def __init__(self, p: ObjectAPI, b: 'RouterBaseInstance'):
        from .. api.map import MapAPI
        super().__init__(b"", loc.ROUTER_SET_TARGET_COMPUTER.encode())
        self.actions = [loc.SELECT.encode()]
        self.description = loc.ROUTER_SET_TARGET_COMPUTER_DESC.encode()
        self.options = [
            OPT("Disable", lambda action: self.set_target_computer(None))
        ]

        for base in MapAPI.instance.query_bases(b.x - 16, b.y - 16, 32, 32):
            if not isinstance(base, ComputerBaseInstance):
                continue
            if base.team != p.get_team():
                continue
            hostname: bytes = base.cpu.get_hostname()
            self.options.append(OPT(hostname.decode(), lambda action: self.set_target_computer(hostname)))

        self.b = b
        self.p = p

    def set_target_computer(self, cpu: Optional[bytes]) -> QueryResponse:
        self.b.target_computer = cpu
        return RouterAdvancedQueryResponse(self.p, self.b)


class RouterAdvancedQueryResponse(QueryResponse):
    def __init__(self, p: ObjectAPI, b: 'RouterBaseInstance'):
        super().__init__(b"", loc.ROUTER_ADVANCED.encode())
        self.actions = [loc.SELECT.encode()]
        self.options = [
            OPT(loc.ROUTER_TARGET_COMPUTER.format(b.target_computer.decode())
                if b.target_computer else loc.ROUTER_SET_TARGET_COMPUTER, self.target_computer),
        ]

        self.options.extend(DeviceQueryResponseOptions.yield_options(b, p))
        self.b = b
        self.p = p

    def target_computer(self, action: bytes) -> QueryResponse:
        return RouterSetTargetComputerQueryResponse(self.p, self.b)


class RouterBaseQueryResponse(QueryResponse):
    def __init__(self, p: ObjectAPI, b: 'RouterBaseInstance'):
        super().__init__(b"", loc.ROUTER.encode()
                         if b.configuration == RouterConfiguration.ROUTER else loc.ROUTER_SPLITTER.encode())

        self.actions = [loc.SELECT.encode()]
        mode = loc.ROUTER_DESC \
            if b.configuration == RouterConfiguration.ROUTER else \
            loc.ROUTER_SPLITTER_DESC
        self.description = loc.ROUTER_DIRECTION.format(mode, b.direction).encode()
        self.options = [
            OPT(loc.ROUTER_SWITCH_SPLITTER
                if b.configuration == RouterConfiguration.ROUTER else loc.ROUTER_SWITCH_ROUTER,
                self.switch_mode),
            OPT("UP", self.switch_direction("up")),
            OPT("DOWN", self.switch_direction("down")),
            OPT("LEFT", self.switch_direction("left")),
            OPT("RIGHT", self.switch_direction("right")),
            OPT(loc.ROUTER_ADVANCED, self.advanced),
        ]

        self.b = b
        self.p = p

    def switch_mode(self, action: bytes):
        self.b.configuration = RouterConfiguration.SPLITTER \
            if self.b.configuration == RouterConfiguration.ROUTER else RouterConfiguration.ROUTER
        self.b.update_icon()

    def advanced(self, action: bytes) -> QueryResponse:
        return RouterAdvancedQueryResponse(self.p, self.b)

    def switch_direction(self, direction) -> Callable:
        def switch(action: bytes):
            self.b.direction = direction
            self.b.update_icon()
        return switch


class RouterBaseInstance(BaseInstance, Device, DeviceAPIHandler):
    ROUTER_ICONS = {
        "up": ROUTER_UP,
        "down": ROUTER_DOWN,
        "left": ROUTER_LEFT,
        "right": ROUTER_RIGHT
    }

    SPLITTER_ICONS = {
        "up": SPLITTER_UP,
        "down": SPLITTER_DOWN,
        "left": SPLITTER_LEFT,
        "right": SPLITTER_RIGHT
    }

    def __init__(self, x: int, y: int, prototype: 'RouterBaseItem', team: Optional['Team'], **kwargs):
        BaseInstance.__init__(self, x, y, prototype, team, **kwargs)
        Device.__init__(self, team.team_id if team else 0, b"router")
        self.direction = "up"
        self.configuration = RouterConfiguration.ROUTER
        self.target_computer: Optional[bytes] = None

    def get_router_block(self) -> 'RouterBlockObject':
        return self.get_block(0, 0)

    def on_device_session(self, session: DeviceAPISession) -> Generator[bytes, bytes, None]:
        action: bytes = (yield)
        b = self.get_router_block()
        if action == b"push":
            direction: bytes = (yield)
            self.direction = direction
            b.move()
        elif action == b"item":
            if b.loaded_items:
                loaded = next(iter(b.loaded_items))
                yield YieldAndClose("{0}\n{1}".format(loaded.item.identity, loaded.amount).encode())
            else:
                yield YieldAndClose(b"\n0")

    def serialize(self) -> Dict[bytes, bytes]:
        d = super().serialize()
        self.dev_serialize(d)
        d[b"dir"] = self.direction.encode()
        d[b"conf"] = self.configuration.encode()
        return d

    def deserialize(self, data: Dict[bytes, bytes]):
        super().deserialize(data)
        self.dev_deserialize(data)
        self.direction = data[b"dir"].decode()
        self.configuration = data[b"conf"].decode()

    def update_icon(self):
        b = self.get_router_block()
        b.code = self.get_icon()
        MapAPI.instance.update_block(b.x, b.y, True)
        self.get_router_block().refresh()

    def get_icon(self) -> int:
        if self.configuration == RouterConfiguration.ROUTER:
            return RouterBaseInstance.ROUTER_ICONS[self.direction]
        else:
            return RouterBaseInstance.SPLITTER_ICONS[self.direction]

    def query_instance(self, player: ObjectAPI) -> Optional['QueryResponse']:
        return RouterBaseQueryResponse(player, self)


class RouterBaseItem(BaseItem):
    def __init__(self, identity: str):
        super().__init__(identity)

    def get_instance(self, x: int, y: int, team: Optional['Team'], **kwargs) -> BaseInstance:
        return RouterBaseInstance(x, y, self, team, **kwargs)


class RouterBlockObject(BaseBlockObject, TubeInterface):
    STACK_LIMIT = 4

    def __init__(self, code: int = 0, base: RouterBaseInstance = None, tag: str = "", proto: Dict[bytes, bytes] = None,
                 **kwargs):
        super().__init__(code, base, proto, **kwargs)
        self.router = base
        self.tag = tag
        self.loaded_items: Set[TubeLoadedItem] = set()
        self.counter = 0

    def accept_neighbor(self, block: 'BlockObject') -> bool:
        return True

    def load(self, loader: 'TubeInterface', tli: TubeLoadedItem):
        if len(self.loaded_items) >= RouterBlockObject.STACK_LIMIT:
            return
        tli.loader = loader
        self.loaded_items.add(tli)
        MapAPI.instance.tubes.register(self)

    def get_routed_block(self) -> Optional[TubeInterface]:
        offset_x, offset_y = direction_to_offset(self.router.direction)
        b = MapAPI.instance.get_block(self.x + offset_x, self.y + offset_y)
        if not isinstance(b, TubeInterface):
            return None
        return b

    def loadable(self, item: Optional['Item'], amount: int, health: float, inside: bool) -> bool:
        return len(self.loaded_items) < RouterBlockObject.STACK_LIMIT

    def unload(self, tli: TubeLoadedItem):
        tli.loader = None
        self.loaded_items.remove(tli)

    def refresh(self):
        super().refresh()
        if len(self.loaded_items) != 0:
            MapAPI.instance.tubes.register(self)

    def check_neighbor(self, x: int, y: int, ls: List[Tuple[TubeInterface, TubeLoadedItem]]):
        b = MapAPI.instance.get_block(self.x + x, self.y + y)
        if b is None:
            return
        if not b.accept_neighbor(self):
            return
        if not isinstance(b, TubeInterface):
            return
        if b.get_tag() != self.tag:
            return
        for i in self.loaded_items:
            if b.loadable(i.item, i.amount, i.health, False):
                ls.append((b, i))

    def move_to_direction(self, direction: str):
        direction_x, direction_y = direction_to_offset(direction)
        neighbors = []
        self.check_neighbor(direction_x, direction_y, neighbors)
        if neighbors:
            chosen, tli = neighbors[0]
            self.unload(tli)
            chosen.load(self, tli)

    def move(self):
        if len(self.loaded_items) == 0:
            return

        if self.router.configuration == RouterConfiguration.ROUTER:
            loaded = next(iter(self.loaded_items))

            if self.router.target_computer:
                def handle(s: DeviceAPISession) -> Generator[bytes, bytes, None]:
                    direction: bytes = (yield)
                    if direction:
                        MapAPI.instance.print("routing to {0}".format(direction.decode()))
                        self.move_to_direction(direction.decode())

                session = self.router.device.connect_to(self.router.target_computer, 1, handle)
                if session is None:
                    self.router.notify_nearby(loc.ROUTER_COMPUTER_ERROR)
                    self.router.target_computer = None
                    return
                session.write_line(b"route")
                session.write_line(loaded.item.identity.encode())
                session.write_line(str(loaded.amount).encode())
                session.write_line(self.router.device.get_hostname())
            else:
                self.move_to_direction(self.router.direction)

        elif self.router.configuration == RouterConfiguration.SPLITTER:
            self.counter += 1
            direction_x, direction_y = direction_to_offset(self.router.direction)
            directions = [(0, 1), (0, -1), (1, 0), (-1, 0)]
            directions.remove((direction_x, direction_y))
            neighbors = []
            for x, y in directions:
                self.check_neighbor(x, y, neighbors)
            if neighbors:
                index = self.counter % len(neighbors)
                chosen, tli = neighbors[index]
                self.unload(tli)
                chosen.load(self, tli)

    def get_x(self) -> int:
        return self.x

    def get_y(self) -> int:
        return self.y

    def get_tag(self) -> str:
        return self.tag


def router_spawner(code: int, tag: str):
    def spawn(bi: RouterBaseInstance) -> BaseBlockObject:
        return RouterBlockObject(code, bi, tag)
    return spawn
