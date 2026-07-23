from typing import Union, Tuple, Dict, List, Optional, TYPE_CHECKING

if TYPE_CHECKING:
    from .. items import Item

from .. import blocks
from . object import ObjectAPI
import random


class BlockObject(object):
    def __init__(self, code: int = 0, item: Optional['Item'] = None, tag: Optional[str] = None,
                 proto: Dict[bytes, bytes] = None, **kwargs):
        self.x = 0
        self.y = 0
        self.code = code
        if proto:
            if b"c" in proto:
                self.code = int.from_bytes(proto[b"c"], "little")
        self._light: int = 0
        self._pressure: int = 0
        self._air: int = 0
        self.item: Optional['Item'] = item
        self.toucher = None
        self.tag: Optional[str] = None

    def set_item(self, item: 'Item'):
        self.item = item

    def accept_neighbor(self, block: 'BlockObject') -> bool:
        return True

    def set_tag(self, s: str):
        self.tag = s

    def reset(self):
        self._light = 0
        self._pressure = 0

    def set_light(self, light: int):
        self._light = light

    def set_pressure(self, pressure_zone: int):
        self._pressure = pressure_zone

    def set_air(self, air: int):
        self._air = air

    def inject_air(self, amount: int):
        self._air = min(self._air + amount, 10)

    def pull_air(self) -> bool:
        if self._air > 0:
            self._air -= 1
            return True
        return False

    def get_air(self) -> int:
        return self._air

    def has_air(self) -> bool:
        return self._air > 0

    def is_air_full(self) -> bool:
        return self._air >= 10

    def reset_pressure(self):
        self._pressure = 0

    def get_light(self) -> int:
        return int(self._light)

    def get_pressure_zone(self) -> int:
        return self._pressure

    def copy_light(self, b: 'BlockObject'):
        self._light = b._light

    def set_location(self, x: int, y: int):
        self.x = x
        self.y = y

    def on_touch(self, player: ObjectAPI) -> bool:
        return False

    def on_overlap(self, player: ObjectAPI) -> bool:
        return False

    def serialize(self) -> Union[bool, Tuple[bytes, Dict[bytes, bytes]]]:
        return self.item.identity.encode(), self.serialize_attrs()

    def serialize_attrs(self) -> Dict[bytes, bytes]:
        d: Dict[bytes, bytes] = dict()
        if self.code:
            d[b"c"] = int.to_bytes(self.code, 2, "little")
        return d

    def get_code(self):
        if not self.code:
            return blocks.EMPTY
        light = self.get_light()
        if light >= blocks.MAXIMUM_LIGHT:
            return self.code
        if light >= 3:
            return self.get_good_light_code(light)
        if light >= 2:
            return self.get_midrange_light_code(light)
        if light >= 1:
            return self.get_some_light_code(light)
        return blocks.EMPTY_BUT_BLOCKING

    def refresh(self):
        pass

    def get_good_light_code(self, light: int):
        if self.code is None:
            return 0
        return self.code

    def get_midrange_light_code(self, light: int):
        if self.code is None:
            return 0
        if self.code & blocks.BLOCKING:
            return blocks.EMPTY_BUT_BLOCKING
        return 0

    def get_some_light_code(self, light: int):
        if self.code is None:
            return 0
        if self.code & blocks.BLOCKING:
            return blocks.EMPTY_BUT_BLOCKING
        return 0

    def blocking(self):
        if self.code is None:
            return 0
        return self.code & blocks.BLOCKING

    def pass_light(self, light: int) -> int:
        return light

    def pass_blocking_light(self, light: int) -> int:
        return light - 1

    def flushable(self):
        return True

    def release(self, notify: bool):
        """
        Called when a block is being disposed of
        """
        pass


class NothingBlockObject(BlockObject):
    def serialize(self) -> Union[bool, Tuple[bytes, Dict[bytes, bytes]]]:
        return False


class NeighboringSet(object):
    def __init__(self, name: str, friendly_list: list, neighbor_codes: dict, default_code: Union[int, list],
                 good_light_codes: Optional[dict] = None,
                 good_light_default: Optional[int] = None,
                 midrange_light_codes: Optional[dict] = None,
                 midrange_light_default: Optional[int] = None,
                 some_light_codes: Optional[dict] = None,
                 some_light_default: Optional[int] = None):
        self.name = name
        self.friendly_list = friendly_list
        self.neighbor_codes = neighbor_codes
        self.good_light_codes = good_light_codes
        self.good_light_default = good_light_default
        self.midrange_light_codes = midrange_light_codes
        self.midrange_light_default = midrange_light_default
        self.some_light_codes = some_light_codes
        self.some_light_default = some_light_default
        self.default_code = default_code


class NeighboringBlockObject(BlockObject):
    def __init__(self, neighboring_set: NeighboringSet, is_blocking: bool = True,
                 proto: Dict[bytes, bytes] = None, **kwargs):
        super(NeighboringBlockObject, self).__init__(code=0, proto=proto, **kwargs)
        self.neighboring_set = neighboring_set
        self.is_blocking = is_blocking
        self.neighbor_code = 0

    def match_block(self, block: BlockObject, value):
        if block is None:
            return 0
        if not block.accept_neighbor(self):
            return 0
        if (block.tag is not None) and (block.tag in self.neighboring_set.friendly_list):
            return value
        if not isinstance(block, NeighboringBlockObject):
            return 0
        if block.neighboring_set.name not in self.neighboring_set.friendly_list:
            return 0
        return value

    def get_good_light_code(self, light: int):
        if self.neighboring_set.good_light_codes:
            return self.neighboring_set.good_light_codes.get(
                self.neighbor_code, self.neighboring_set.good_light_default)
        if self.neighboring_set.good_light_default is not None:
            return self.neighboring_set.good_light_default
        return self.code

    def get_midrange_light_code(self, light: int):
        if self.neighboring_set.midrange_light_codes:
            return self.neighboring_set.midrange_light_codes.get(
                self.neighbor_code, self.neighboring_set.midrange_light_default)
        if self.neighboring_set.midrange_light_default is not None:
            return self.neighboring_set.midrange_light_default
        return self.code

    def get_some_light_code(self, light: int):
        if self.neighboring_set.some_light_codes:
            return self.neighboring_set.some_light_codes.get(
                self.neighbor_code, self.neighboring_set.some_light_default)
        if self.neighboring_set.some_light_default is not None:
            return self.neighboring_set.some_light_default
        return self.code

    def refresh(self):
        from . map import MapAPI

        top_block = MapAPI.instance.get_block(self.x, self.y - 1)
        bottom_block = MapAPI.instance.get_block(self.x, self.y + 1)
        left_block = MapAPI.instance.get_block(self.x - 1, self.y)
        right_block = MapAPI.instance.get_block(self.x + 1, self.y)

        self.neighbor_code = self.match_block(top_block, 1) + self.match_block(right_block, 2) + \
            self.match_block(bottom_block, 4) + self.match_block(left_block, 8)

        if self.neighbor_code not in self.neighboring_set.neighbor_codes:
            l1 = self.neighboring_set.default_code
        else:
            l1 = self.neighboring_set.neighbor_codes[self.neighbor_code]

        if isinstance(l1, list):
            if self.code not in l1:
                self.code = random.choice(l1)
        else:
            self.code = l1

    def blocking(self):
        return self.is_blocking

    @staticmethod
    def __check_and_update_block__(x: int, y: int, notify: bool):
        from . map import MapAPI

        if isinstance(MapAPI.instance.get_block(x, y), NeighboringBlockObject):
            MapAPI.instance.update_block(x, y, notify)

    def refresh_neighbors(self, notify: bool):
        NeighboringBlockObject.__check_and_update_block__(self.x, self.y - 1, notify)
        NeighboringBlockObject.__check_and_update_block__(self.x, self.y + 1, notify)
        NeighboringBlockObject.__check_and_update_block__(self.x - 1, self.y, notify)
        NeighboringBlockObject.__check_and_update_block__(self.x + 1, self.y, notify)

    def release(self, notify: bool):
        self.refresh_neighbors(notify)
