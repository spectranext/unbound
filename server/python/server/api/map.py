from typing import Callable, Optional, Dict, List, TYPE_CHECKING
import time

from . object import ObjectAPI
from . client import ClientAPI
from . block import BlockObject
from . computer import ComputerAPI
from . device import DeviceAPI, DeviceAPIHandler
from .. deadline import Deadlines
from .. scenarios import get_scenario
from .. import loc

if TYPE_CHECKING:
    from .. bases.bases import BaseInstance


class MapAPI(object):
    instance: 'MapAPI' = None

    def __init__(self):
        MapAPI.instance = self

        # disallow placing a block higher than this value
        self.top_placing_block = 0
        self.day = None

        from .. bases.bases import Bases
        from .. tube import TubeManager
        from .. liquid import Liquids
        from .. air import Air
        from .. scenarios import Scenario

        self.counter = 0
        self.bases = Bases()
        self.tubes = TubeManager()
        self.liquids = Liquids()
        self.air = Air()
        self.scenario = b""
        self.deadlines: Optional[Deadlines] = None
        self.scenario_obj: Optional[Scenario] = None
        self.map_generation_angle: Optional[float] = None
        self.day_cycle_started_at: Optional[int] = None

    def ensure_day_cycle_anchor(self, start_in_daytime: bool = False):
        if self.day_cycle_started_at is not None:
            return

        now = int(time.time())
        if not start_in_daytime:
            self.day_cycle_started_at = now
            return

        sc = get_scenario(self.scenario)
        # Start the map around midday so a fresh game begins during daylight.
        self.day_cycle_started_at = now - ((sc.seconds_in_a_day * 3) // 4)

    def get_cycle_elapsed_seconds(self) -> int:
        self.ensure_day_cycle_anchor()
        return max(0, int(time.time()) - int(self.day_cycle_started_at))

    def get_phase(self) -> float:
        sc = get_scenario(self.scenario)
        return (self.get_cycle_elapsed_seconds() % sc.seconds_in_a_day) / sc.seconds_in_a_day

    def is_day(self) -> bool:
        sc = get_scenario(self.scenario)
        return (self.get_cycle_elapsed_seconds() % sc.seconds_in_a_day) > (sc.seconds_in_a_day // 2)

    def is_cold(self) -> bool:
        return not self.is_day()

    def get_phase_name(self) -> str:
        phase_index = min(5, int(6 * self.get_phase()))
        # Align phase labels with the displayed 24h clock (00:00 starts at "Midnight")
        return loc.DAILY_CYCLE_PHASES[(phase_index + 1) % len(loc.DAILY_CYCLE_PHASES)]

    def get_phase_seconds(self) -> int:
        sc = get_scenario(self.scenario)
        return self.get_cycle_elapsed_seconds() % sc.seconds_in_a_day

    def get_seconds_in_day(self) -> int:
        sc = get_scenario(self.scenario)
        return sc.seconds_in_a_day

    def get_time_hours(self) -> int:
        return int(24 * self.get_phase())

    def get_time_minutes(self) -> int:
        sc = get_scenario(self.scenario)
        seconds_per_hour = max(1, sc.seconds_in_a_day // 24)
        hour_progress = self.get_phase_seconds() % seconds_per_hour
        return int((hour_progress * 60) / seconds_per_hour)

    def on_init(self):
        self.print("Map init complete.")

        from .. team import TEAMS

        self.deadlines = Deadlines(self.pause_deadlines)

        for t in TEAMS:
            t.init()

    def on_chat_message(self, author: ClientAPI, message: bytes):
        pass

    def pause_deadlines(self) -> bool:
        return len(self.query_clients()) == 0

    def on_shutdown(self):
        pass

    def serialize(self, spath: bytes):
        pass

    def allocate_tag(self) -> str:
        pass

    def allocate_link(self) -> int:
        pass

    def obtain_link(self, link: int) -> Optional[List]:
        pass

    def deallocate_link(self, link: int):
        pass

    def pre_deserialize(self, spath: bytes):
        pass

    def post_deserialize(self, spath: bytes):
        pass

    def cache_client(self, client: ClientAPI):
        pass

    def query_cache(self, client: ClientAPI) -> Optional[Dict]:
        pass

    def query_bases(self, x: int, y: int, w: int, h: int) -> List['BaseInstance']:
        pass

    def print(self, log: str): ...
    def shutdown(self): ...
    def get_width(self) -> int: ...
    def get_height(self) -> int: ...
    def set_block(self, x: int, y: int, block: 'BlockObject', notify: bool): ...
    def update_block(self, x: int, y: int, notify: bool): ...
    def get_block(self, x: int, y: int) -> 'BlockObject': ...
    def set_new_client_callback(self, f: Callable[[ClientAPI, bytes], None]): ...
    def schedule_map_refresh(self, immediate: bool): ...
    def schedule_block_method(self, x: int, y: int, call_after: int, method: bytes): ...
    def schedule_callback(self, cb: Callable, call_after: int): ...
    def send_effect(self, x: float, y: float, effect: bytes): ...
    def iterate(self, from_x: int, from_y: int, to_x: int, to_y: int, cb: Callable[['BlockObject'], None]): ...
    def spawn_object(self, x: float, y: float, kind: bytes) -> ObjectAPI: ...
    def spawn_player(self, client: ClientAPI, x: int, y: int) -> ObjectAPI: ...
    def query_objects(self, x: int, y: int, w: int, h: int) -> List[ObjectAPI]: ...
    def get_object(self, object_id: int) -> Optional[ObjectAPI]: ...
    def query_clients(self) -> List[ClientAPI]: ...
    def get_client(self, client_id: int) -> Optional[ClientAPI]: ...
    def query_team_computers(self, team_id: int) -> List[ComputerAPI]: ...
    def computer_new(self, namespace_id: int, computer_hash: Optional[bytes]) -> ComputerAPI: ...
    def computer_find(self, computer_hash: bytes) -> Optional[ComputerAPI]: ...
    def device_new(self, namespace_id: int, hostname_prefix: bytes) -> DeviceAPI: ...
    def add_bullet(self, x: float, y: float, team_id: int, damage: int, angle: int, sound: Optional[int], effect: Optional[bytes]): ...
