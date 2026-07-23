from typing import Set, Dict, Tuple, Optional, List, TYPE_CHECKING

if TYPE_CHECKING:
    from . items import Item

from . api.map import MapAPI
from . bases import BaseBlockObject, BaseInstance
from . api.block import BlockObject, NeighboringBlockObject, NeighboringSet


class TubeManager(object):
    def __init__(self):
        self.full_tubes: Set[TubeInterface] = set()

    def register(self, tb: 'TubeInterface'):
        self.full_tubes.add(tb)

    def update(self):
        tubes = self.full_tubes
        self.full_tubes = set()
        for tb in tubes:
            tb.move()


class TubeLoadedItem(object):
    def __init__(self, loader: 'TubeInterface', item: 'Item', amount: int, health: float):
        self.loader = loader
        self.item = item
        self.amount = amount
        self.health = health


class TubeInterface(object):
    def load(self, loader: 'TubeInterface', tli: TubeLoadedItem):
        pass

    def loadable(self, item: Optional['Item'], amount: int, health: float, inside: bool) -> bool:
        pass

    def unload(self, tli: TubeLoadedItem):
        pass

    def move(self):
        pass

    def get_x(self) -> int:
        pass

    def get_y(self) -> int:
        pass

    def get_tag(self) -> str:
        pass


class TubeBlockObject(NeighboringBlockObject, TubeInterface):
    def __init__(self, empty: NeighboringSet, filled: NeighboringSet, tag: str, stack_limit=4,
                 proto: Dict[bytes, bytes] = None, **kwargs):
        super().__init__(empty, is_blocking=False, proto=proto, **kwargs)
        self.empty = empty
        self.filled = filled
        self.tag = tag
        self.stack_limit = stack_limit
        self.loaded_items: Set[TubeLoadedItem] = set()

    def load(self, loader: 'TubeInterface', tli: TubeLoadedItem):
        if len(self.loaded_items) >= self.stack_limit:
            return
        tli.loader = loader
        self.loaded_items.add(tli)
        self.mark_filled(True)
        MapAPI.instance.tubes.register(self)

    def flushable(self):
        return False

    def get_x(self) -> int:
        return self.x

    def get_y(self) -> int:
        return self.y

    def unload(self, tli: TubeLoadedItem):
        tli.loader = None
        self.loaded_items.remove(tli)
        self.mark_filled(len(self.loaded_items) > 0)

    def loadable(self, item: Optional['Item'], amount: int, health: float, inside: bool) -> bool:
        return len(self.loaded_items) < self.stack_limit

    def get_tag(self) -> str:
        return self.tag

    def check_neighbor(self, x: int, y: int, ls: List[Tuple[TubeInterface, TubeLoadedItem]]):
        b = MapAPI.instance.get_block(x, y)
        if b is None:
            return
        if not b.accept_neighbor(self):
            return
        if not isinstance(b, TubeInterface):
            return
        if b.get_tag() != self.tag:
            return
        for i in self.loaded_items:
            if b == i.loader:
                continue
            if b.loadable(i.item, i.amount, i.health, False):
                ls.append((b, i))

    def refresh(self):
        super().refresh()
        if len(self.loaded_items) != 0:
            MapAPI.instance.tubes.register(self)

    def move(self):
        if len(self.loaded_items) == 0:
            self.mark_filled(False)
            return

        # special transit case
        if self.neighbor_code in [7, 11, 13, 14, 15]:
            to_move: List[Tuple[TubeInterface, TubeLoadedItem]] = []
            for tli in self.loaded_items:
                if tli.loader is None:
                    continue
                x = tli.loader.get_x()
                y = tli.loader.get_y()
                diff_x = self.x - x
                diff_y = self.y - y
                chosen = MapAPI.instance.get_block(self.x + diff_x, self.y + diff_y)
                if chosen is None:
                    continue
                if not chosen.accept_neighbor(self):
                    continue
                if not isinstance(chosen, TubeInterface):
                    continue
                if chosen.get_tag() != self.tag:
                    continue
                if not chosen.loadable(tli.item, tli.amount, tli.health, False):
                    continue
                # transfer onto opposite side
                to_move.append((chosen, tli))

            # defer the reload due to changing list of loaded_items
            for chosen, tli in to_move:
                self.unload(tli)
                chosen.load(self, tli)

            return

        neighbors: List[Tuple[TubeInterface, TubeLoadedItem]] = []

        self.check_neighbor(self.x - 1, self.y, neighbors)
        self.check_neighbor(self.x, self.y - 1, neighbors)
        self.check_neighbor(self.x + 1, self.y, neighbors)
        self.check_neighbor(self.x, self.y + 1, neighbors)

        if len(neighbors) == 0:
            return

        # supposed to be only one neighbor
        chosen, tli = neighbors[0]
        self.unload(tli)
        chosen.load(self, tli)

    def mark_filled(self, filled: bool):
        self.neighboring_set = self.filled if filled else self.empty
        MapAPI.instance.update_block(self.x, self.y, True)


class TubeInletBlockObject(BaseBlockObject, TubeInterface):
    def __init__(self, code: int = 0, base: BaseInstance = None,
                 tag: str = "", direction: str = "", proto: Dict[bytes, bytes] = None, **kwargs):
        super().__init__(code=code, base=base, proto=proto, **kwargs)
        self.direction = direction
        self.tag = tag

    def accept_neighbor(self, block: 'BlockObject') -> bool:
        if self.direction == "up":
            return block.x == self.x and block.y == self.y - 1
        elif self.direction == "down":
            return block.x == self.x and block.y == self.y + 1
        elif self.direction == "left":
            return block.x == self.x - 1 and block.y == self.y
        elif self.direction == "right":
            return block.x == self.x + 1 and block.y == self.y
        else:
            return False

    def load(self, loader: 'TubeInterface', tli: TubeLoadedItem):
        if self.base.can_accept_inlet_item(tli.item, tli.amount, tli.health):
            self.base.accept_inlet_item(tli.item, tli.amount, tli.health)

    def loadable(self, item: Optional['Item'], amount: int, health: float, inside: bool) -> bool:
        if inside:
            return False
        return self.base.can_accept_inlet_item(item, amount, health)

    def unload(self, tli: TubeLoadedItem):
        # doesn't store anything
        pass

    def move(self):
        # this item only accepts items
        pass

    def get_x(self) -> int:
        return self.x

    def get_y(self) -> int:
        return self.y

    def get_tag(self) -> str:
        return self.tag


def tube_inlet_spawner(code: int, tag: str, direction: str):
    def spawn(bi: BaseInstance) -> BaseBlockObject:
        return TubeInletBlockObject(code, bi, tag, direction)
    return spawn


class TubeOutletBlockObject(BaseBlockObject, TubeInterface):
    def __init__(self, code: int = 0, base: BaseInstance = None, tag: str = "", direction: str = "",
                 proto: Dict[bytes, bytes] = None, **kwargs):
        super().__init__(code=code, base=base, proto=proto, **kwargs)
        self.tag = tag
        self.direction = direction
        self.loaded_item: Optional[TubeLoadedItem] = None

    def load(self, loader: 'TubeInterface', tli: TubeLoadedItem):
        if loader != self and self.base.can_accept_inlet_item(tli.item, tli.amount, tli.health):
            self.base.accept_inlet_item(tli.item, tli.amount, tli.health)
        else:
            self.loaded_item = tli
            tli.loader = loader
            MapAPI.instance.tubes.register(self)

    def accept_neighbor(self, block: 'BlockObject') -> bool:
        if self.direction == "up":
            return block.x == self.x and block.y == self.y - 1
        elif self.direction == "down":
            return block.x == self.x and block.y == self.y + 1
        elif self.direction == "left":
            return block.x == self.x - 1 and block.y == self.y
        elif self.direction == "right":
            return block.x == self.x + 1 and block.y == self.y
        else:
            return False

    def refresh(self):
        if self.loaded_item:
            MapAPI.instance.tubes.register(self)

    def get_x(self) -> int:
        return self.x

    def get_y(self) -> int:
        return self.y

    def loadable(self, item: Optional['Item'], amount: int, health: float, inside: bool) -> bool:
        # you cannot load into outlet, unless it comes from the base itself
        if inside:
            return self.loaded_item is None
        else:
            return self.base.can_accept_inlet_item(item, amount, health)

    def unload(self, tli: TubeLoadedItem):
        self.loaded_item = None

    def check_neighbor(self, x: int, y: int) -> Optional[TubeInterface]:
        b = MapAPI.instance.get_block(x, y)
        if not isinstance(b, TubeInterface):
            return None
        if b.get_tag() != self.tag:
            return None
        if not b.loadable(self.loaded_item.item, self.loaded_item.amount, self.loaded_item.health, False):
            return None
        return b

    def move(self):
        if self.loaded_item is None:
            return
        if self.direction == "up":
            b = self.check_neighbor(self.x, self.y - 1)
        elif self.direction == "left":
            b = self.check_neighbor(self.x - 1, self.y)
        elif self.direction == "right":
            b = self.check_neighbor(self.x + 1, self.y)
        elif self.direction == "down":
            b = self.check_neighbor(self.x, self.y + 1)
        else:
            return

        if b is None:
            return

        i = self.loaded_item
        self.unload(self.loaded_item)
        b.load(self, i)

    def get_tag(self) -> str:
        return self.tag


def tube_outlet_spawner(code: int, tag: str, direction: str):
    def spawn(bi: BaseInstance) -> BaseBlockObject:
        return TubeOutletBlockObject(code, bi, tag, direction)
    return spawn
