from typing import Optional, Generator, List, Tuple, Union, TYPE_CHECKING, Dict
from collections import deque
from . import BotPlayerObject
from .. api.object import ObjectAPI
from .. api.map import MapAPI
from .. api.client import ClientAPI
from .. api.block import BlockObject, NeighboringBlockObject
from .. api.touch import PostponedTouch
from .. import items
from .. import loc
from .. api.query import QueryResponse, OPT
from .. inventory import InventoryRecord, Inventory
from .. api.computer import ComputerAPI, MountedXFSFile, MountedXFSFolder
from .. blockspawn import BlockSpawn
from .. import hit

if TYPE_CHECKING:
    from .. team import Team
    from .. player import PlayerObject

import time
import random
import functools


class VirtualStaticFile(MountedXFSFile):
    data = b""

    def __init__(self, path: bytes, flags: int):
        self.offset = 0

    def read(self, size: int) -> bytes:
        offset = self.offset
        data = self.data[offset:offset + size]
        self.offset = offset + len(data)
        return data

    def write(self, data: bytes) -> int:
        return 0

    def seek(self, mode: int, offset: int) -> int:
        if mode == 0:
            self.offset = offset
        elif mode == 1:
            self.offset += offset
        elif mode == 2:
            self.offset = len(self.data) + offset
        else:
            return -22

        if self.offset < 0:
            self.offset = 0
        return self.offset

    def close(self):
        pass


class VirtualVersionFile(VirtualStaticFile):
    data = b"1\r\n"


class BlocksSnapshotFile(VirtualStaticFile):
    SIZE = 21
    RADIUS = SIZE // 2
    SNAPSHOT_SIZE = SIZE * SIZE * 5

    def __init__(self, bot: 'Bot'):
        self.offset = 0
        self.data = self.build_snapshot(bot)

    @classmethod
    def build_snapshot(cls, bot: 'Bot') -> bytes:
        server_map = MapAPI.instance
        center_x = int(bot.get_x())
        center_y = int(bot.get_y())
        lines = []

        for dy in range(-cls.RADIUS, cls.RADIUS + 1):
            y = center_y + dy
            for dx in range(-cls.RADIUS, cls.RADIUS + 1):
                x = center_x + dx
                code = 0
                if 0 <= x < server_map.get_width() and 0 <= y < server_map.get_height():
                    block = server_map.get_block(x, y)
                    if block is not None and block.code:
                        code = block.code & 0xFF
                lines.append("{0:03d}\r\n".format(code).encode())

        return b"".join(lines)


class ObjectsSnapshotFile(VirtualStaticFile):
    MAX_OBJECTS = 10
    DISTANCE = 64

    def __init__(self, bot: 'Bot'):
        self.offset = 0
        self.data = self.build_snapshot(bot)

    @classmethod
    def build_snapshot(cls, bot: 'Bot') -> bytes:
        server_map = MapAPI.instance
        bot_x = bot.get_x()
        bot_y = bot.get_y()
        bot_team = bot.get_team()
        distance_sq = cls.DISTANCE * cls.DISTANCE

        objects = []
        for o in server_map.query_objects(
            int(bot_x) - cls.DISTANCE,
            int(bot_y) - cls.DISTANCE,
            cls.DISTANCE * 2,
            cls.DISTANCE * 2
        ):
            if o == bot:
                continue
            dx = o.get_x() - bot_x
            dy = o.get_y() - bot_y
            d = (dx * dx) + (dy * dy)
            if d > distance_sq:
                continue
            objects.append((d, o))

        objects.sort(key=lambda entry: entry[0])

        lines = []
        for _, o in objects[:cls.MAX_OBJECTS]:
            object_type = o.identity() or b"unknown"
            object_team = o.get_team()
            enemy = 1 if object_team is not None and bot_team is not None and object_team != bot_team else 0
            lines.extend([
                object_type + b"\r\n",
                "{0}\r\n".format(o.get_id()).encode(),
                "{0}\r\n".format(int(o.get_x())).encode(),
                "{0}\r\n".format(int(o.get_y())).encode(),
                "{0}\r\n".format(enemy).encode(),
            ])

        return b"".join(lines)


class MoveCommandFile(VirtualStaticFile):
    def __init__(self, bot: 'Bot'):
        self.offset = 0
        self.data = b""
        self.bot = bot

    def write(self, data: bytes) -> int:
        self.data += data
        return len(data)

    def close(self):
        try:
            parts = self.data.replace(b",", b" ").split()
            if len(parts) < 2:
                self.bot.set_api_error()
                return
            self.bot.command_move_to(int(parts[0]), int(parts[1]))
        except ValueError:
            self.bot.set_api_error()


class TwoNumberSnapshotFile(VirtualStaticFile):
    SNAPSHOT_SIZE = 14

    def __init__(self, a: int, b: int):
        self.offset = 0
        self.data = "{0:05d}\r\n{1:05d}\r\n".format(a % 100000, b % 100000).encode()


class VirtualDirectory(MountedXFSFolder):
    entries: List[bytes] = []

    def __init__(self, path: bytes, flags: int):
        self.offset = 0

    def readdir(self):
        offset = self.offset
        if offset >= len(self.entries):
            return None
        self.offset = offset + 1
        return self.entries[offset]

    def close(self):
        pass


class ApiDirectory(VirtualDirectory):
    def __init__(self, bot: 'Bot'):
        self.offset = 0
        self.entries = [
            (b"blocks", False, BlocksSnapshotFile.SNAPSHOT_SIZE),
            (b"objects", False, len(ObjectsSnapshotFile.build_snapshot(bot))),
            (b"move", False, 0),
            (b"location", False, TwoNumberSnapshotFile.SNAPSHOT_SIZE),
            (b"size", False, TwoNumberSnapshotFile.SNAPSHOT_SIZE),
        ]


class BotSetHostnameQueryResponse(QueryResponse):
    def __init__(self, bi: 'Bot', player: 'PlayerObject'):
        super().__init__(b"", loc.COMPUTER_NEW_HOSTNAME.encode())
        self.bi = bi

        self.player = player
        self.edit = True
        self.actions = [loc.OK.encode()]

    def selected(self, option: int, action: bytes) -> Optional['QueryResponse']:
        self.bi.cpu.set_hostname(action)
        return BotQuery(self.bi, self.player)


class BotPostMessageQueryResponse(QueryResponse):
    def __init__(self, bi: 'Bot', player: 'PlayerObject'):
        super().__init__(b"", loc.COMPUTER_POST_MESSAGE.encode())
        self.bi = bi

        self.player = player
        self.edit = True
        self.actions = [loc.OK.encode()]

    def selected(self, option: int, action: bytes) -> Optional['QueryResponse']:
        message = action.decode().replace(",", "\n")
        self.bi.cpu.post_message("{0}\n".format(message).encode())
        return BotQuery(self.bi, self.player)


class BotQuery(QueryResponse):
    def __init__(self, bot: 'Bot', player: 'PlayerObject'):
        super().__init__(b"", "BOT | Power: {0}".format(bot.power).encode())
        self.bot = bot
        self.player = player
        if not self.bot.power:
            self.description = loc.BOT_NO_POWER.encode()
        else:
            if self.bot.power_on:
                self.image = self.bot.cpu.get_image()
            else:
                self.description = loc.BOT_TURNED_OFF.encode()

        self.options = [
            OPT(loc.BOT_CONTROL, self.remote_control),
            OPT(loc.COMPUTER_TURN_OFF if self.bot.power_on else loc.COMPUTER_TURN_ON, self.switch_power),
            OPT(loc.BOT_FOLLOW_ME if self.bot.following is None else loc.BOT_FOLLOW_STOP, self.switch_follow),
            OPT(loc.BOT_COMPUTER, self.open),
            OPT(loc.COMPUTER_REBOOT, self.reboot),
            OPT(loc.COMPUTER_HOSTNAME.format(self.bot.cpu.get_hostname().decode()), self.set_hostname),
            OPT(loc.COMPUTER_NMI, self.nmi),
            OPT(loc.COMPUTER_POST_MESSAGE, self.post_message),
        ]
        self.actions = [loc.OK.encode()]

    def switch_power(self, action: bytes) -> Optional[QueryResponse]:
        self.bot.power_on = not self.bot.power_on
        return None

    def switch_follow(self, action: bytes) -> Optional[QueryResponse]:
        if self.bot.following is None:
            self.bot.following = self.player.get_id()
        else:
            self.bot.following = None
        return None

    def remote_control(self, action: bytes) -> Optional[QueryResponse]:
        def do():
            self.bot.set_control_by(self.player.client)
        MapAPI.instance.schedule_callback(do, 100)
        return None

    def reboot(self, action: bytes) -> Optional[QueryResponse]:
        self.bot.cpu.reboot()
        return self.open(action)

    def open(self, action: bytes) -> Optional[QueryResponse]:
        from .. player import PlayerObject
        from .. api.map import MapAPI

        if not isinstance(self.player, PlayerObject):
            return None

        c = self.player.client
        if c is None:
            return None

        MapAPI.instance.schedule_callback(
            functools.partial(c.computer_session, self.bot.cpu, None), 500)

        return None

    def nmi(self, action: bytes) -> Optional[QueryResponse]:
        self.bot.cpu.nmi()
        return self.open(action)

    def set_hostname(self, action: bytes) -> Optional[QueryResponse]:
        return BotSetHostnameQueryResponse(self.bot, self.player)

    def post_message(self, action: bytes) -> Optional[QueryResponse]:
        return BotPostMessageQueryResponse(self.bot, self.player)


class SelfControlQuery(QueryResponse):
    def __init__(self, bot: 'Bot'):
        super().__init__(b"", b"Bot")
        self.bot = bot
        self.options = [
            OPT(loc.BOT_CONTROL_CANCEL, self.remote_control)
        ]
        self.actions = [loc.OK.encode()]

    def remote_control(self, action: bytes) -> Optional[QueryResponse]:
        MapAPI.instance.schedule_callback(self.bot.release_control_by, 100)
        return None


class Bot(BotPlayerObject):
    BOT = b"bot"
    COUNT = 0

    PORT_API_VERSION = 100
    PORT_CURRENT_MODE = 101
    PORT_MOVE_RIGHT = 102
    PORT_MOVE_LEFT = 103
    PORT_MOVE_UP = 104
    PORT_MOVE_DOWN = 105
    PORT_MOVE_JUMP = 106
    PORT_MY_HEALTH = 107
    PORT_MY_ENERGY = 108
    PORT_ENEMIES_NEARBY = 109
    PORT_FRIENDLIES_NEARBY = 110
    PORT_CURSOR_X = 111
    PORT_CURSOR_Y = 112
    PORT_COMMAND = 113
    PORT_BLOCK_ID = 114
    PORT_INVENTORY_COUNT = 115

    def __init__(self, damage_value: int, health_value: int, data_entry: bytes, move_entry: bytes):
        BotPlayerObject.__init__(self,0, data_entry, move_entry, None)
        self.counter = 0
        self.cooldown = None
        self.contact_cooldown = 1
        self.following: Optional[int] = None
        self.damage_value = damage_value
        self.health = health_value
        self.direction = False
        self.direction_time = 0
        self.counter_time = 10
        self.following_time = None
        self.time_to_follow = 1.5
        self.time_to_break_after_follow = 3
        self.query_time = 5
        self.hit_charge = 0
        self.hit_charge_to = 20
        self.team: Optional['Team'] = None
        self.control: Optional[int] = None
        self.inventory = Inventory()
        self.selected_record: Optional['InventoryRecord'] = None
        self.state = b"BOT"
        self.cpu: Optional[ComputerAPI] = None
        self.queried_objects: Optional[List[ObjectAPI]] = None
        self.next_object: Optional[ObjectAPI] = None
        self.mode = "idle"
        self.going_target: Optional[Tuple[int, int]] = None
        self.going_path: List[Tuple[int, int]] = []
        self.going_path_time = 0.0
        self.going_path_interval = 0.25
        self.going_arrival_distance = 1.0

        self.power_on = True
        self.power_consumption = 30
        self.power_test = time.time() + self.power_consumption
        self.api_mode = 0
        self.api_cursor_x = 0
        self.api_cursor_y = 0
        self.api_block_id = 0
        Bot.COUNT += 1

    def on_init(self, server_map: 'MapAPI'):
        super().on_init(server_map)

        def schedule():
            self.cpu = MapAPI.instance.computer_new(0 if self.team is None else self.team.team_id, None)
            self.cpu.bind_port_read(Bot.PORT_API_VERSION, self.port_api_version)
            self.cpu.bind_port_read(Bot.PORT_CURRENT_MODE, self.port_current_mode)
            self.cpu.bind_port_read(Bot.PORT_MY_HEALTH, self.port_my_health)
            self.cpu.bind_port_read(Bot.PORT_MY_ENERGY, self.port_my_energy)
            self.cpu.bind_port_read(Bot.PORT_ENEMIES_NEARBY, functools.partial(self.port_nearby_count, False))
            self.cpu.bind_port_read(Bot.PORT_FRIENDLIES_NEARBY, functools.partial(self.port_nearby_count, True))
            self.cpu.bind_port_read(Bot.PORT_BLOCK_ID, self.port_block_id_read)
            self.cpu.bind_port_read(Bot.PORT_INVENTORY_COUNT, self.port_inventory_count)
            self.cpu.bind_port_write(Bot.PORT_MOVE_RIGHT, self.port_move_right)
            self.cpu.bind_port_write(Bot.PORT_MOVE_LEFT, self.port_move_left)
            self.cpu.bind_port_write(Bot.PORT_MOVE_UP, self.port_move_up)
            self.cpu.bind_port_write(Bot.PORT_MOVE_DOWN, self.port_move_down)
            self.cpu.bind_port_write(Bot.PORT_MOVE_JUMP, self.port_move_jump)
            self.cpu.bind_port_write(Bot.PORT_CURSOR_X, self.port_cursor_x)
            self.cpu.bind_port_write(Bot.PORT_CURSOR_Y, self.port_cursor_y)
            self.cpu.bind_port_write(Bot.PORT_COMMAND, self.port_command)
            self.cpu.bind_port_write(Bot.PORT_BLOCK_ID, self.port_block_id_write)
            self.cpu.mount_path(b"/api", lambda path, flags: ApiDirectory(self))
            self.cpu.mount_path(b"/api/blocks", lambda path, flags: BlocksSnapshotFile(self))
            self.cpu.mount_path(b"/api/objects", lambda path, flags: ObjectsSnapshotFile(self))
            self.cpu.mount_path(b"/api/move", lambda path, flags: MoveCommandFile(self))
            self.cpu.mount_path(b"/api/location", lambda path, flags: TwoNumberSnapshotFile(int(self.get_x()), int(self.get_y())))
            self.cpu.mount_path(b"/api/size", lambda path, flags: TwoNumberSnapshotFile(MapAPI.instance.get_width(), MapAPI.instance.get_height()))


        MapAPI.instance.schedule_callback(schedule, 1)

    def object_scan_distance(self) -> int:
        return 10

    def query_objects(self, friendly: bool):
        d = self.object_scan_distance()

        def _filter(o: ObjectAPI) -> bool:
            if o == self:
                return False
            o_friendly = o.get_team() == self.get_team()
            return o_friendly == friendly

        self.queried_objects = list(filter(_filter, MapAPI.instance.query_objects(
            int(self.get_x()) - d,
            int(self.get_y()) - d,
            d * 2, d * 2
        )))

    @staticmethod
    def api_signed_byte(value: int) -> int:
        value = value & 0xFF
        return value - 0x100 if value & 0x80 else value

    @staticmethod
    def clamp_port_value(value: int) -> int:
        return max(0, min(255, int(value)))

    def set_api_error(self):
        self.api_mode = 3

    def set_api_idle(self):
        self.api_mode = 0

    def api_cursor_location(self) -> Tuple[int, int]:
        return int(self.get_x()) + self.api_cursor_x, int(self.get_y()) + self.api_cursor_y

    def api_item_from_block_id(self, block_id: int) -> Optional[items.Item]:
        for item in items.Item.ITEMS.values():
            if item.pure_icon() == (block_id & 0xFF):
                return item
        return None

    def api_nearby_objects(self, friendly: bool) -> List[ObjectAPI]:
        d = ObjectsSnapshotFile.DISTANCE
        bot_team = self.get_team()
        if bot_team is None:
            return []
        result = []
        for o in MapAPI.instance.query_objects(int(self.get_x()) - d, int(self.get_y()) - d, d * 2, d * 2):
            if o == self:
                continue
            object_team = o.get_team()
            if object_team is None:
                continue
            same_team = object_team == bot_team
            if same_team == friendly:
                result.append(o)
        return result

    def command_move_to(self, x: int, y: int):
        self.api_mode = 1
        self.go_location(x, y)

    def command_collect(self, x: int, y: int):
        self.api_block_id = 0
        if abs(x - int(self.get_x())) > 1 or abs(y - int(self.get_y())) > 1:
            self.set_api_error()
            return
        block = MapAPI.instance.get_block(x, y)
        if block is None or not block.code or not block.item:
            self.set_api_error()
            return
        block_id = block.item.pure_icon()
        try:
            remove_block, time_to_remove, relevant_record = block.item.remove(self)
        except items.PlacingError:
            self.set_api_error()
            return

        if time_to_remove > 0:
            self.set_api_error()
            return

        if relevant_record and relevant_record.item.degradation_per_hit:
            relevant_record.health -= relevant_record.item.degradation_per_hit
            if relevant_record.health <= 0:
                self.remove_record_from_inventory(relevant_record, 1)

        if remove_block.credits:
            if self.get_team() is not None:
                self.get_team().add_credits(remove_block.credits)
        else:
            self.add_to_inventory(remove_block, 1, 1., notify=False)

        new_block = BlockObject(0, items.NOTHING)
        new_block.copy_light(block)
        MapAPI.instance.set_block(x, y, new_block, True)
        new_block.refresh()

        if isinstance(new_block, NeighboringBlockObject):
            new_block.refresh_neighbors(True)

        MapAPI.instance.schedule_map_refresh(False)
        self.api_block_id = block_id
        self.set_api_idle()

    def command_place(self, x: int, y: int):
        item = self.api_item_from_block_id(self.api_block_id)
        if item is None:
            self.set_api_error()
            return
        record = self.inventory.get_record(item)
        if record is None:
            self.set_api_error()
            return
        block = MapAPI.instance.get_block(x, y)
        if block is None or block.code:
            self.set_api_error()
            return

        try:
            placed_item = item.place(self, x, y)
        except items.PlacingError:
            self.set_api_error()
            return

        if not self.remove_from_inventory(item):
            self.set_api_error()
            return
        if placed_item is None:
            self.set_api_error()
            return

        new_block = BlockSpawn.create_block(placed_item)
        if new_block is None:
            self.set_api_error()
            return

        new_block.copy_light(block)
        MapAPI.instance.set_block(x, y, new_block, True)
        new_block.refresh()

        if isinstance(new_block, NeighboringBlockObject):
            new_block.refresh_neighbors(True)

        MapAPI.instance.schedule_map_refresh(False)
        self.set_api_idle()

    def port_api_version(self) -> int:
        return 1

    def port_current_mode(self) -> int:
        if self.api_mode == 3:
            return 3
        if self.api_mode == 2:
            return 2
        if self.mode == "going_to_target":
            return 1
        return 0

    def port_my_health(self) -> int:
        return self.clamp_port_value(self.health)

    def port_my_energy(self) -> int:
        return self.clamp_port_value(self.power)

    def port_nearby_count(self, friendly: bool) -> int:
        return self.clamp_port_value(len(self.api_nearby_objects(friendly)))

    def port_cursor_x(self, data: int):
        self.api_cursor_x = self.api_signed_byte(data)

    def port_cursor_y(self, data: int):
        self.api_cursor_y = self.api_signed_byte(data)

    def port_command(self, data: int):
        x, y = self.api_cursor_location()
        if data == 1:
            self.command_move_to(x, y)
        elif data == 2:
            self.command_collect(x, y)
        elif data == 3:
            self.command_place(x, y)
        else:
            self.set_api_error()

    def port_block_id_write(self, data: int):
        self.api_block_id = data & 0xFF

    def port_block_id_read(self) -> int:
        return self.api_block_id & 0xFF

    def port_inventory_count(self) -> int:
        item = self.api_item_from_block_id(self.api_block_id)
        if item is None:
            return 0
        return self.clamp_port_value(self.inventory.count_items(item))

    def port_move_right(self, data: int):
        self.move_to(self.get_x() + data, self.get_y(), 0)

    def port_move_left(self, data: int):
        self.move_to(self.get_x() - data, self.get_y(), 0)

    def port_move_up(self, data: int):
        self.move_to(self.get_x(), self.get_y() + data, 0)

    def port_move_down(self, data: int):
        self.move_to(self.get_x(), self.get_y() - data, 0)

    def port_move_jump(self, data: int):
        self.move_to(self.get_x(), self.get_y(), 1)

    def get_team(self) -> Optional['Team']:
        return self.team

    def set_team(self, team: 'Team'):
        self.team = team
        self.reset_object_state()

    def calculate_power_consumption(self):
        return 0
        res = 1
        if self.cpu.is_powered_on():
            res += 4
        return res

    def serialize(self) -> Dict[bytes, bytes]:
        d = super().serialize()
        d[b'cpu'] = self.cpu.serialize()
        return d

    def deserialize(self, data: Dict[bytes, bytes]):
        if b'cpu' in data:
            self.cpu.deserialize(data[b'cpu'])
        super().deserialize(data)

    def on_killed(self):
        super().on_killed()
        if self.cpu:
            self.cpu.destroy()
            self.cpu = None
        if self.control:
            self.release_control_by()
        Bot.COUNT -= 1

    def identity(self) -> bytes:
        return Bot.BOT

    def generate_drop(self) -> Generator[Tuple[items.Item, int, float], None, None]:
        pass

    def validate_player_query(self, player: 'ObjectAPI') -> bool:
        from .. player import PlayerObject
        if player == self:
            return True
        if not isinstance(player, PlayerObject):
            return False
        if player.get_team() != self.get_team():
            return False
        return True

    def on_player_query(self, player: 'ObjectAPI') -> Optional[QueryResponse]:
        from .. player import PlayerObject
        if player == self:
            return SelfControlQuery(self)
        if not isinstance(player, PlayerObject):
            return None
        return BotQuery(self, player)

    def on_query(self, q: bytes) -> Optional[QueryResponse]:
        from .. query import query

        # Active object while remote-controlling is the bot; query() handles "status"
        # only for PlayerObject, so expose cancel here.
        if self.control is not None and q == b"status":
            return SelfControlQuery(self)
        return query(self, q, 8)

    def get_client_id(self) -> int:
        if self.control:
            return self.control
        return 0

    def get_inventory(self) -> Optional['Inventory']:
        return self.inventory

    def get_selected_record(self) -> Optional['InventoryRecord']:
        return self.selected_record

    def select_record(self, record: 'InventoryRecord'):
        self.selected_record = record

    def add_to_inventory(self, item: items.Item, amount: int, health: float, notify: bool = True):
        new_amount, new_record = self.inventory.add_item(item, amount, health)
        if notify:
            client = self.get_client()
            if client:
                client.queue_notify(
                    "+{0} {1}, total: {2}".format(amount, item.name, new_amount).encode(),
                    ClientAPI.NOTIFY_MESSAGE_COLOR_REGULAR)
        elif self.selected_record is None:
            self.selected_record = new_record

    def remove_record_from_inventory(self, record: 'InventoryRecord', remove_amount: int = 1) -> Union[bool, float]:
        remaining = self.inventory.remove_from_record(record, remove_amount)
        upd = False
        if remaining == 0 and self.selected_record is not None:
            if self.selected_record == record:
                self.selected_record = self.inventory.get_record(record.item)
                upd = True
        return remaining

    def remove_from_inventory(self, item: items.Item, remove_amount: int = 1) -> Union[bool, float]:
        yielded_health, remaining = self.inventory.remove_item(item, remove_amount)
        if yielded_health == 0:
            return False
        upd = False
        if remaining == 0 and self.selected_record is not None:
            if self.selected_record.item == item:
                self.selected_record = self.inventory.get_record(item)
        return yielded_health

    def on_touch(self, x: int, y: int) -> Union[None, PostponedTouch]:
        return hit.touch(self, x, y)

    def set_control_by(self, client: ClientAPI):
        if self.control is not None:
            return
        self.control = client.client_id
        client.set_object_control(self.get_id())
        self.state = loc.BOT_CONTROL.encode()
        o = client.get_client_object()
        if o:
            o.set_object_state(ObjectAPI.OBJECT_STATE_CONTROL)
        client.sync_stats()

    def release_control_by(self):
        if self.control is None:
            return None
        c = MapAPI.instance.get_client(self.control)
        if c:
            o = c.get_client_object()
            if o:
                o.reset_object_state()
            c.set_object_control(0)
            c.sync_stats()
        self.control = None

    def go_location(self, x: int, y: int):
        self.going_target = (int(x), int(y))
        self.going_path = []
        self.going_path_time = 0.0
        self.following = None
        self.mode = "going_to_target"

    def stop_going_to_target(self):
        if self.mode == "going_to_target":
            self.mode = "idle"
        self.going_target = None
        self.going_path = []

    def follow_mode(self, server_map: MapAPI):
        self.direction_time -= 1
        if self.direction_time <= 0:
            self.direction_time = random.randint(20, 50)
            self.direction = random.randint(0, 10) >= 5
        if self.following:
            self.follow_locked()

    def _can_path_through(self, server_map: MapAPI, x: int, y: int) -> bool:
        if x < 0 or y < 0 or x >= server_map.get_width() or y >= server_map.get_height():
            return False
        block = server_map.get_block(x, y)
        return bool(block is not None and not block.blocking())

    def _find_path_to_target(self, server_map: MapAPI, target: Tuple[int, int]) -> Optional[List[Tuple[int, int]]]:
        start = (int(self.get_x()), int(self.get_y()))
        if start == target:
            return []
        if not self._can_path_through(server_map, target[0], target[1]):
            return None

        min_x = max(0, min(start[0], target[0]) - 40)
        max_x = min(server_map.get_width() - 1, max(start[0], target[0]) + 40)
        min_y = max(0, min(start[1], target[1]) - 25)
        max_y = min(server_map.get_height() - 1, max(start[1], target[1]) + 25)

        queue = deque([start])
        came_from: Dict[Tuple[int, int], Optional[Tuple[int, int]]] = {start: None}

        while queue:
            current = queue.popleft()
            if current == target:
                break

            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                next_point = (current[0] + dx, current[1] + dy)
                if next_point in came_from:
                    continue
                if next_point[0] < min_x or next_point[0] > max_x:
                    continue
                if next_point[1] < min_y or next_point[1] > max_y:
                    continue
                if not self._can_path_through(server_map, next_point[0], next_point[1]):
                    continue
                came_from[next_point] = current
                queue.append(next_point)

        if target not in came_from:
            return None

        path = []
        current = target
        while current != start:
            path.append(current)
            current = came_from[current]
        path.reverse()
        return path

    def going_to_target_mode(self, server_map: MapAPI):
        if self.going_target is None:
            self.stop_going_to_target()
            return

        dx = self.going_target[0] - self.get_x()
        dy = self.going_target[1] - self.get_y()
        if abs(dx) <= self.going_arrival_distance and abs(dy) <= self.going_arrival_distance:
            self.stop_going_to_target()
            return

        now = time.time()
        if now >= self.going_path_time:
            path = self._find_path_to_target(server_map, self.going_target)
            if path is None:
                self.stop_going_to_target()
                return
            self.going_path = path
            self.going_path_time = now + self.going_path_interval

        while self.going_path:
            next_x, next_y = self.going_path[0]
            if abs(next_x - self.get_x()) <= 0.5 and abs(next_y - self.get_y()) <= 0.5:
                self.going_path.pop(0)
            else:
                break

        if not self.going_path:
            self.move_to(self.going_target[0], self.going_target[1], self.going_target[1] < self.get_y())
            return

        next_x, next_y = self.going_path[0]
        self.move_to(next_x, next_y, next_y < self.get_y())

    def on_update(self, server_map: MapAPI):
        super().on_update(server_map)
        if self.power and self.mode == "going_to_target":
            self.going_to_target_mode(server_map)
        elif self.power and (self.control is None):
            self.follow_mode(server_map)

        tm = time.time()
        if tm > self.power_test:
            self.power_test = tm + self.power_consumption
            if self.power > 0:
                self.power = max(self.power - self.calculate_power_consumption(), 0)

        if self.cpu:
            if self.power_on and self.power:
                self.cpu.set_power(True)
            else:
                self.cpu.set_power(False)

    def follow_locked(self):
        if self.is_contact_cooldown_active():
            return
        o = MapAPI.instance.get_object(self.following)
        if o:
            if self.following_time is None:
                self.following_time = time.time() + self.time_to_follow
            else:
                if time.time() > self.following_time:
                    self.following_time = None
                    self.cooldown = time.time() + self.time_to_break_after_follow
                    return
            self.move_to(o.get_x(), o.get_y(), o.get_y() < self.get_y())
        else:
            self.following = None
            self.counter = self.counter_time

    def is_contact_cooldown_active(self) -> bool:
        if self.cooldown:
            if time.time() > self.cooldown:
                self.cooldown = None
                return False
            else:
                return True
        return False

    def on_contact(self, server_map: 'MapAPI', o: 'ObjectAPI'):
        pass
