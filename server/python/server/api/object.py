from typing import Optional, Dict, Union, Generator, Tuple, TYPE_CHECKING
import time

from . touch import PostponedTouch
from . query import QueryResponse
from .. inventory import Inventory
from .. effects import MapEffects

if TYPE_CHECKING:
    from . map import MapAPI
    from .. team import Team
    from .. items import Item
    from . client import ClientAPI
    from .. inventory import InventoryRecord


class ObjectAPI(object):
    OBJECT_TYPE_PLAYER = 0x01
    OBJECT_TYPE_STATIC = 0x02
    OBJECT_TYPE_SLOW = 0x04
    OBJECT_TYPE_COLLIDER = 0x08
    OBJECT_TYPE_SPRITE = 0x10

    MOTION_PROFILE_NONE = 0
    MOTION_PROFILE_SLOWING_DOWN = 1
    MOTION_PROFILE_ACCELERATING = 2

    OBJECT_STATE_NORMAL = 0
    OBJECT_STATE_CONTROL = 1
    OBJECT_STATE_PICKING = 2

    COLLISION_BOTTOM = 0x01
    COLLISION_LEFT = 0x02
    COLLISION_RIGHT = 0x04
    COLLISION_TOP = 0x08

    DATA_ENTRY_CHAR1 = b"CHAR1"
    DATA_ENTRY_CHAR1_MOVING = b"CHAR1MOV"
    DATA_ENTRY_CHAR1_PICKING = b"CHAR1PCK"
    DATA_ENTRY_CHAR2 = b"CHAR2"
    DATA_ENTRY_SLIME = b"SLIME"
    DATA_ENTRY_SPIDER = b"SPIDER"
    DATA_ENTRY_BOT = b"BOT"
    DATA_ENTRY_SLIME_MOVING = b"SLIMEMOV"
    DATA_ENTRY_SPIDER_MOVING = b"SPDMOV"
    DATA_ENTRY_BOT_MOVING = b"BOTMOV"
    DATA_ENTRY_SHIP1 = b"SHIP1"
    DATA_ENTRY_ROCKET = b"ROCK"

    def __init__(self, object_type: int, data_entry: bytes = None, move_entry: bytes = None,
                 picking_entry: Optional[bytes] = None):
        self.object_type = object_type
        self.data_entry = data_entry
        self.move_entry = move_entry
        self.picking_entry = picking_entry
        self.health = 100
        self.temperature = 100
        self.heat_time = 0
        self.power = self.get_max_power()
        self.power_consumption = 10
        self.hit_auto = 0
        self.hit_delay = 0
        self.default_state: bytes = b"?"
        self.building_state: bytes = b"?"
        self.state: bytes = self.default_state
        self.flashing = None
        self.neutral = False
        self.last_damage_owner: Optional['ObjectAPI'] = None
        self.last_damage_reason: Optional[str] = None
        self.active_postponed_touch: Optional['PostponedTouch'] = None

    def identity(self) -> bytes:
        pass

    def is_full_power(self) -> bool:
        return self.power >= self.get_max_power()

    def get_max_power(self) -> int:
        return 100

    def get_max_temperature(self) -> int:
        return 100

    def get_min_temperature(self) -> int:
        return 0

    def serialize(self) -> Dict[bytes, bytes]:
        return {
            b'_': self.identity(),
            b'H': self.health.to_bytes(2, "little"),
            b'P': self.power.to_bytes(2, "little"),
            b'S': self.state
        }

    def deserialize(self, data: Dict[bytes, bytes]):
        self.health = int.from_bytes(data.get(b'H'), "little") if b'H' in data else 0
        self.power = int.from_bytes(data.get(b'P'), "little") if b'P' in data else 0
        self.state = data.get(b'S', b'?')

    def is_flashing(self) -> bool:
        return bool(self.flashing)

    def get_state_flags(self) -> int:
        flags = 0
        if self.flashing:
            flags |= ObjectAPI.STATE_FLAG_FLASHING
        return flags

    def set_active_postponed_touch(self, touch: 'PostponedTouch'):
        self.active_postponed_touch = touch

    def update_state_flags(self):
        self.set_state_flags(self.get_state_flags())

    def update_state(self):
        pass

    def on_update(self, server_map: 'MapAPI'):
        if self.flashing:
            if time.time() > self.flashing:
                self.flashing = None
                self.update_state_flags()

    STATE_FLAG_FLASHING = 0x01
    STATE_FLAG_AIM = 0x02

    def on_init(self, server_map: 'MapAPI'):
        pass

    def on_sync(self, server_map: 'MapAPI'):
        pass

    def on_block_collide(self, server_map: 'MapAPI'):
        # only called on OBJECT_TYPE_COLLIDER type objects
        pass

    def on_fall(self, server_map: 'MapAPI', fall_speed: int):
        pass

    def on_touch(self, x: int, y: int) -> Union[None, PostponedTouch]:
        return None

    def on_aim(self, angle: int):
        return None

    def on_hit(self, angle: int):
        pass

    def on_query(self, q: bytes) -> Optional[QueryResponse]:
        return None

    def on_contact(self, server_map: 'MapAPI', o: 'ObjectAPI'):
        pass

    def validate_player_query(self, player: 'ObjectAPI') -> bool:
        return False

    def on_player_query(self, player: 'ObjectAPI') -> Optional[QueryResponse]:
        return None

    def on_moved(self):
        pass

    def get_flash_time(self):
        return 2

    def flash_for_a_while(self):
        self.flashing = time.time() + self.get_flash_time()
        self.set_state_flags(ObjectAPI.STATE_FLAG_FLASHING)

    def kill(self):
        self.health = 0
        self.on_killed()
        drop = Inventory()
        for item, amount, health in self.generate_drop():
            drop.add_item(item, amount, health)
        if drop.has_something():
            self.drop(drop)
        self.last_damage_owner = None
        self.last_damage_reason = None
        self.destroy()

    def on_killed(self):
        from . map import MapAPI
        x = self.get_x()
        y = self.get_y()

        def sh():
            MapAPI.instance.send_effect(x, y, MapEffects.EXPLOSION)
        MapAPI.instance.schedule_callback(sh, 150)

    def generate_drop(self) -> Generator[Tuple['Item', int, float], None, None]:
        return
        yield

    def get_team(self) -> Optional['Team']:
        return None

    def set_team(self, team: 'Team'):
        pass

    def get_client_id(self) -> int:
        pass

    def get_client(self) -> Optional['ClientAPI']:
        from . map import MapAPI
        client_id = self.get_client_id()
        if not client_id:
            return None
        return MapAPI.instance.get_client(client_id)

    def get_inventory(self) -> Optional[Inventory]:
        return None

    def get_selected_record(self) -> Optional['InventoryRecord']:
        return None

    def select_record(self, record: 'InventoryRecord'):
        pass

    def add_to_inventory(self, item: 'Item', amount: int, health: float, notify: bool = True):
        pass

    def remove_record_from_inventory(self, record: 'InventoryRecord', remove_amount: int = 1) -> Union[bool, float]:
        pass

    def remove_from_inventory(self, item: 'Item', remove_amount: int = 1) -> Union[bool, float]:
        pass

    def get_team_id(self) -> int:
        t = self.get_team()
        if t is None:
            return 0
        return t.team_id

    def drop(self, inventory: Inventory):
        from .. bases import spawn_base, BaseBlockObject
        from .. bases.inventory import InventoryBaseInstance
        from .. items import get_item
        from .. api.map import MapAPI

        checklist = sorted(
            [(dx, dy) for dy in range(-3, 4) for dx in range(-3, 4)],
            key=lambda p: (abs(p[0]) + abs(p[1]), max(abs(p[0]), abs(p[1])), abs(p[1]), abs(p[0]), p[1], p[0])
        )

        x, y = int(self.get_x()), int(self.get_y())

        for dx, dy in checklist:
            b = MapAPI.instance.get_block(x + dx, y + dy)
            if isinstance(b, BaseBlockObject) and isinstance(b.base, InventoryBaseInstance):
                for r in inventory.entries.values():
                    b.base.inventory.add_item(r.item, r.amount, r.health)
                break
            if b and b.code:
                continue
            b_below = MapAPI.instance.get_block(x + dx, y + dy + 1)
            if (b_below is None) or (not b_below.blocking()):
                continue
            # noinspection PyTypeChecker
            bi: InventoryBaseInstance = spawn_base(
                get_item("chest"), x + dx, y + dy, self.get_team(),
                inventory=inventory, quick_pickup=True)
            bi.neutral = True
            bi.on_init()
            break

    def damage(self, value: int, reason: Optional[str] = None, owner: Optional['ObjectAPI'] = None) -> bool:
        if self.is_flashing():
            return False
        if reason:
            self.last_damage_reason = reason
        if owner:
            self.last_damage_owner = owner
        if self.health <= value:
            self.kill()
            return False
        self.flash_for_a_while()
        self.health -= value
        self.on_damaged()
        return True

    def is_player(self) -> bool:
        return False

    def is_alive(self) -> bool:
        return self.health > 0

    def on_damaged(self):
        pass

    def get_object_state(self) -> int:
        return ObjectAPI.OBJECT_STATE_NORMAL

    def set_state(self, default_state: str, building_state: str):
        self.default_state = default_state.encode()
        self.building_state = building_state.encode()

    def heal(self, value: int) -> bool:
        if self.health >= 100:
            return False
        self.health = min(self.health + value, 100)
        self.on_healed()
        return True

    def apply_heat(self, time_for: int) -> bool:
        if self.temperature >= 100:
            return False
        self.heat_time += time_for
        self.on_heated()
        return True

    def on_healed(self):
        pass

    def on_heated(self):
        pass

    # do not override the following
    def looking_left(self) -> bool: ...
    def has_collision(self, side: int) -> bool: ...
    def get_id(self) -> int: ...
    def get_x(self) -> float: ...
    def get_y(self) -> float: ...
    def get_speed_x(self) -> int: ...
    def get_speed_y(self) -> int: ...
    def set_speed(self, x: int, y: int): ...
    def move_to(self, x: float, y: float): ...
    def set_motion_profile(self, profile: int): ...
    def jump(self, power: float): ...
    def set_location(self, x: float, y: float): ...
    def destroy(self): ...
    def set_state_flags(self, flags: int): ...
    def set_sprite_offset(self, sprite_offset: int): ...
    def set_object_state(self, state: int): ...
    def reset_object_state(self): ...
