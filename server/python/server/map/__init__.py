from typing import List, Dict, Optional, Tuple

import pickle
import os

from ..api.client import ClientAPI
from .. api.map import MapAPI
from .. api.computer import ComputerAPI
from .. tuning import Tuning
from .. bot.slime import Slime
from .. team import TEAMS

from . refresh import refresh_map
from . generate import generate_objects, generate_map
from . debug import debug_map
from . shutdown import shutdown_map

# needed to register item types for load
from .. bases import all, BaseInstance


class ServerMap(MapAPI):
    def __init__(self):
        super().__init__()
        self.next_tag = 1
        self.next_link = 1
        self.links: Dict[int, List] = {}
        self.client_cache: Dict[bytes, Dict] = {}
        self.slime_spawn_state: str = "wait_night"

    def allocate_tag(self) -> str:
        s = str(self.next_tag)
        self.next_tag += 1
        return s

    def allocate_link(self) -> int:
        self.links[self.next_link] = list()
        result = self.next_link
        self.next_link += 1
        return result

    def obtain_link(self, link: int) -> Optional[List]:
        if link not in self.links:
            new_list = list()
            self.links[link] = new_list
            return new_list
        return self.links[link]

    def deallocate_link(self, link: int):
        if link in self.links:
            del self.links[link]

    def serialize(self, spath: bytes):
        data = [
            b.serialize()
            for b in self.bases.entries.values()
        ]

        teams_data = [
            team.serialize()
            for team in TEAMS
        ]

        p = os.path.join(spath.decode(), "root")
        self.print("Saving bases to {0}".format(p))

        for c in MapAPI.instance.query_clients():
            self.cache_client(c)

        root = {
            "bases": data,
            "teams": teams_data,
            "next_tag": self.next_tag.to_bytes(4, "little"),
            "next_link": self.next_link.to_bytes(4, "little"),
            "client_cache": self.client_cache,
            "day_cycle_started_at": self.day_cycle_started_at
        }

        with open(p, "wb") as f:
            pickle.dump(root, f, protocol=pickle.HIGHEST_PROTOCOL)

        self.print("serialized {0} bases".format(len(data)))
        self.print("serialized {0} teams".format(len(teams_data)))

    def cache_client(self, client: ClientAPI):
        self.client_cache[client.get_user_id()] = client.serialize()
        MapAPI.instance.print(f"Cached client {client.get_user_id()}")

    def query_cache(self, client: ClientAPI) -> Optional[Dict]:
        return self.client_cache.get(client.get_user_id())

    def query_bases(self, x: int, y: int, w: int, h: int) -> List['BaseInstance']:
        result: List[BaseInstance] = []
        for b in self.bases.entries.values():
            if x + w < b.x:
                continue
            if y + h < b.y:
                continue
            if x > b.x + b.prototype.width:
                continue
            if y > b.y + b.prototype.height:
                continue
            result.append(b)
        return result

    def deserialize(self, spath: bytes):
        from .. items import Item
        from .. team import Team, find_team
        from .. bases import BaseInstance, BaseItem, spawn_base

        p = os.path.join(spath.decode(), "root")
        self.print("Loading bases from {0}".format(p))

        with open(p, "rb") as f:
            root: Dict = pickle.load(f)

        self.next_tag = int.from_bytes(root["next_tag"], "little")
        self.next_link = int.from_bytes(root["next_link"], "little")
        self.client_cache = root["client_cache"]
        self.day_cycle_started_at = root.get("day_cycle_started_at")

        if "teams" in root:
            teams_data: List[Dict] = root["teams"]
            for i, team_data in enumerate(teams_data):
                if i < len(TEAMS):
                    TEAMS[i].deserialize(team_data)
            self.print("deserialized {0} teams".format(len(teams_data)))

        if "bases" in root:
            data: List[Dict[bytes, bytes]] = root["bases"]

            counter = 0

            for entry in data:
                identity, tm, x, y = BaseInstance.parse_base(entry)
                team: Optional[Team] = find_team(tm) if tm is not None else None
                item: Optional[Item] = Item.ITEMS.get(identity.decode())
                if item is None:
                    self.print("unknown base: {0}".format(identity.decode()))
                    continue
                if not isinstance(item, BaseItem):
                    self.print("not a base: {0}".format(item.name))
                    continue
                bi = spawn_base(item, x, y, team)
                bi.deserialize(entry)
                bi.on_init()
                counter += 1

            self.print("deserialized {0} bases".format(counter))

    def on_chat_message(self, author: ClientAPI, message: bytes):
        full_message = b"%b: %b" % (author.get_name(), message)
        author_name = author.get_name().decode("utf-8", errors="replace")
        message_text = message.decode("utf-8", errors="replace")
        clients = self.query_clients()

        self.print(
            "chat: received from {0} ({1} clients): {2}".format(
                author_name, len(clients), message_text))

        for client in clients:
            client_name = client.get_name().decode("utf-8", errors="replace")
            blocked_notifications = ",".join(
                b.decode("utf-8", errors="replace")
                for b in getattr(client, "blocked_notifications", set())
            )
            self.print(
                "chat: sending to {0} client_id={1} notify_blocked=[{2}]".format(
                    client_name, client.client_id, blocked_notifications))
            client.send_chat_message(full_message)
            if client != author:
                client.queue_notify(full_message, ClientAPI.NOTIFY_MESSAGE_COLOR_REGULAR)

        from ..http.web_chat import publish_chat_message_to_web

        publish_chat_message_to_web(
            author.get_name().decode("utf-8", errors="replace"),
            message.decode("utf-8", errors="replace"),
        )


def create_map() -> MapAPI:
    return ServerMap()


def init_map(server_map: MapAPI, scenario: bytes):
    from .. newclient import new_client
    from .. http.server import start_http_server, start_reporter
    from .. scenarios import get_scenario
    from .. team import configure_teams_for_scenario
    server_map.scenario = scenario
    server_map.scenario_obj = get_scenario(scenario)
    configure_teams_for_scenario(server_map.scenario_obj)
    server_map.set_new_client_callback(new_client)
    start_http_server()
    start_reporter()
    server_map.on_init()


def update_slime_spawn_cycle(server_map: ServerMap):
    if not server_map.scenario_obj or not server_map.scenario_obj.bots:
        return

    if not Tuning.SLIME_NIGHT_ONLY:
        if Slime.COUNT < Tuning.SLIME_COUNT:
            Slime.spawn_missing(server_map)
        return

    state = server_map.slime_spawn_state

    if state == "wait_night":
        if not server_map.is_day():
            server_map.slime_spawn_state = "spawning"
            server_map.print("slimes: spawning")
        return

    if state == "spawning":
        if server_map.is_day():
            server_map.slime_spawn_state = "wait_night"
            server_map.print("slimes: waiting for night")
            return

        # spawn_missing may fail to place slimes (no free slots, terrain, players nearby).
        # Never loop unbounded: that freezes the server inside update_map.
        while Slime.COUNT < Tuning.SLIME_COUNT:
            before = Slime.COUNT
            Slime.spawn_missing(server_map)
            if Slime.COUNT <= before:
                break

        server_map.slime_spawn_state = "wait_day"
        server_map.print("slimes: spawned, waiting for day")
        return

    if state == "wait_day":
        if server_map.is_day():
            server_map.slime_spawn_state = "wait_night"
            server_map.print("slimes: its day, waiting for night")
        return

    server_map.slime_spawn_state = "wait_night"


def update_map(server_map: MapAPI):

    server_map.counter += 1
    # every 5 seconds
    if server_map.counter % 100 == 0:
        for team in TEAMS:
            team.update()
    # every second
    if server_map.counter % 20 == 0:
        server_map.bases.update_bases()
        # Update worm digging tracker
        from .. bot.worms import WormDiggingTracker
        WormDiggingTracker.get_instance().update(server_map)
    # every half a second
    if server_map.counter % 10 == 0:
        server_map.tubes.update()
    # every half quarter second
    if server_map.counter % 5 == 0:
        server_map.liquids.update()

    if server_map.counter > 100:
        # noinspection PyTypeChecker
        update_slime_spawn_cycle(server_map)
