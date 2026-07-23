import functools
import os
import random
from typing import Optional, Callable, Tuple, TYPE_CHECKING

from . api.map import MapAPI
from . api.client import ClientAPI
from . api.query import QueryResponse, QueryResponseOption
from . player import PlayerObject
from . player.email import EmailSession
from . bot.ship import Ship
from . import items
from . art import CONTRACT, SELECTION, OPERATIONAL_FUNDS
from . imagegen import Image
from . team import Team, TEAMS
from . scenarios import get_scenario
from . import loc
from . loc import emails

if TYPE_CHECKING:
    from . player.client import Client


class TeamSelectionOption(QueryResponseOption):
    def __init__(self, team: Team):
        self.team = team

    def icon(self):
        return self.team.icon

    def __str__(self) -> str:
        return "{0}".format(self.team.name)


class TeamSelection(QueryResponse):
    def __init__(self, c: ClientAPI, done: Callable):
        super().__init__(b"", loc.CONTRACT_WITH.encode(), SELECTION)
        self.c = c
        self.done = done
        self.description = loc.CONTRACT_WITH_DESC.encode()
        self.options = [
            TeamSelectionOption(team)
            for team in TEAMS
        ]
        self.actions = [loc.CHOOSE.encode()]

    def quick_cancel(self) -> bool:
        return False

    def cancelled(self) -> Optional['QueryResponse']:
        # can't cancel us
        return self

    def selected(self, option: int, action: bytes) -> Optional['QueryResponse']:
        if option >= len(self.options):
            return TeamSelection(self.c, self.done)
        o = self.options[option]
        o.team.add_member(self.c)
        self.done()
        self.done = None


class WelcomeMessage(QueryResponse):
    def __init__(self, c: ClientAPI, done: Callable):
        super().__init__(b"", loc.NEW_CLIENT_WELCOME.encode(), CONTRACT)
        self.c = c
        self.done = done
        self.description = loc.NEW_CLIENT_WELCOME_DESC.encode()
        self.actions = [loc.NEW_CLIENT_WELCOME_CHOOSE.encode()]

    def quick_cancel(self) -> bool:
        return False

    def cancelled(self) -> Optional['QueryResponse']:
        # can't cancel us
        return self

    def selected(self, option: int, action: bytes) -> Optional['QueryResponse']:
        return TeamSelection(self.c, self.done)


def new_client(c: 'Client', scenario: bytes):
    server_map = MapAPI.instance
    sc = get_scenario(scenario)

    def log_player_portal_link():
        report_url = os.environ.get("REPORT_URL")
        address = os.environ.get("REPORT_ADDRESS")
        port = os.environ.get("REPORT_PORT")
        if not report_url or not address or not port:
            return

        from .http.rpc import compute_server_hash, report_web_url
        from .http.web_actions.player import decode_bytes, player_context

        server_hash = os.environ.get("REPORT_SERVER_HASH", compute_server_hash(address, port))
        name = decode_bytes(c.get_name())
        url = report_web_url(
            report_url,
            server_hash,
            "player",
            name,
            context=player_context(c),
        )
        server_map.print(f"Player portal for {name}: {url}")

    if scenario != b"respawn":
        log_player_portal_link()

    def find_ground_y(x: int) -> int:
        y = 0
        for y in range(0, server_map.get_height()):
            b = server_map.get_block(x, y)
            b1 = server_map.get_block(x + 1, y)
            if (b and b.blocking()) or (b1 and b1.blocking()):
                return y - 2
        return y

    def find_nearby_landing_spot(origin_x: int, preferred_offset: int = 4) -> Tuple[int, int]:
        width = server_map.get_width()
        candidates = [preferred_offset, -preferred_offset, preferred_offset + 2, -(preferred_offset + 2)]

        for delta in candidates:
            x = max(0, min(width - 2, origin_x + delta))
            y = find_ground_y(x)
            if y >= 0:
                return x, y

        x = max(0, min(width - 2, origin_x))
        return x, find_ground_y(x)

    def spawn_bot_delivery(team: Team, origin_x: int):
        if team.bot_delivered:
            return

        bot_x, bot_y = find_nearby_landing_spot(origin_x)

        def bot_ship_fly_off(ship: Ship):
            ship.fly_off()

        def do_spawn_bot(ship: Ship):
            if team.bot_delivered:
                server_map.schedule_callback(functools.partial(bot_ship_fly_off, ship), 2000)
                return

            bot = server_map.spawn_object(int(ship.get_x()), int(ship.get_y()), b"bot")
            if bot:
                bot.set_team(team)
                team.bot_delivered = True
            server_map.schedule_callback(functools.partial(bot_ship_fly_off, ship), 2000)

        def do_bot_ship(x3: int, y3: int):
            # noinspection PyTypeChecker
            ship: Ship = server_map.spawn_object(x3, max(y3 - 10, 1), b"ship1")

            def landed():
                server_map.schedule_callback(functools.partial(do_spawn_bot, ship), 2000)

            ship.set_on_landed(landed)

        server_map.schedule_callback(functools.partial(do_bot_ship, bot_x, bot_y), 2000)

    def schedule_free_bot_email():
        team = c.get_team()
        player = c.player
        if not team or not player or team.bot_delivered:
            return

        def deliver():
            current_team = c.get_team()
            current_player = c.player
            if not current_team or not current_player or current_team.bot_delivered:
                return

            def claim_bot():
                latest_team = c.get_team()
                latest_player = c.player
                if not latest_team or latest_team.bot_delivered or not latest_player:
                    return None
                spawn_bot_delivery(latest_team, int(latest_player.get_x()))
                return None

            c.add_email_template(
                emails.FREE_BOT,
                action_name=b"CLAIM BOT",
                action=claim_bot
            )

        server_map.schedule_callback(deliver, 5000)

    def post_spawn(o: PlayerObject):
        if o is None:
            server_map.print(f"post_spawn skipped for client {c.get_user_id()}: player spawn failed")
            return
        if not c.welcome_email_sent:
            c.add_email_template(emails.NEW_CLIENT)
            c.welcome_email_sent = True
        c.weapon_consume_refill()
        server_map.schedule_callback(o.update_state, 100)
        server_map.deadlines.update()

    cached = server_map.query_cache(c)
    if cached and (b"player" in cached):
        c.deserialize(cached)
        if c.has_team():
            c.get_team().add_member(c)
        x = int(cached[b"x"])
        y = int(cached[b"y"])
        player = cached[b"player"]

        def watch():
            c.stop_music()
            c.force_watch(x, y)
            c.play_playlist()
            c.player = server_map.spawn_player(c, x, y)
            if c.player:
                c.player.deserialize(player)

        # schedule because c gets assigned
        server_map.schedule_callback(watch, 500)

        server_map.print(f"Restored client {c.get_user_id()} from cache")
        server_map.deadlines.update()
        return

    if c.equipped_weapon is None:
        # Give a handgun
        c.equipped_weapon = items.get("hg1")

    if sc.spawn_immediately:
        if len(TEAMS) > 1:
            if len(TEAMS[1].members) >= len(TEAMS[0].members):
                TEAMS[0].add_member(c)
            else:
                TEAMS[1].add_member(c)
        else:
            TEAMS[0].add_member(c)

        x = int(server_map.get_width() * c.get_team().location)
        x = random.randint(x - 8, x + 8)
        y = 0
        for y in range(0, server_map.get_height()):
            b = server_map.get_block(x, y)
            b1 = server_map.get_block(x + 1, y)
            if (b and b.blocking()) or (b1 and b1.blocking()):
                y -= 2
                break

        def watch():
            c.stop_music()
            c.force_watch(x, y)
            c.play_playlist()
            c.player = server_map.spawn_player(c, x, y)
            if c.player is None:
                server_map.print(f"Immediate spawn failed for client {c.get_user_id()}")
                return
            post_spawn(c.player)
            schedule_free_bot_email()

        # schedule because c gets assigned
        server_map.schedule_callback(watch, 500)
    else:
        def tablet():
            if not c.tutorial:
                c.queue_notify(
                    loc.NEW_CLIENT_TABLET.encode(),
                    ClientAPI.NOTIFY_MESSAGE_COLOR_BRIGHT,
                    context="new_client_tablet",
                    trigger_immediately=True)
                server_map.schedule_callback(tablet, 30000)

        def self_fly_off(ship: Ship):
            ship.fly_off()
            if not sc.respawn:
                server_map.schedule_callback(schedule_free_bot_email, 2000)
            server_map.schedule_callback(tablet, 15000)

        def do_spawn2(x2, y2, ship: Ship):
            # noinspection PyTypeChecker
            try:
                o: PlayerObject = server_map.spawn_player(c, x2, y2)
                c.player = o
                if c.player is None:
                    server_map.print(f"Ship spawn failed for client {c.get_user_id()}")
                else:
                    post_spawn(c.player)
            except Exception as e:
                server_map.print(f"Ship spawn failed for client {c.get_user_id()}: {e}")
            finally:
                self_fly_off(ship)

        def do_spawn(x2: int, y2: int, ship: Ship):
            x2 = int(ship.get_x())
            c.force_watch(x2, y2)
            do_spawn2(x2, y2, ship)

        def do_ship(x: int, y: int):
            # noinspection PyTypeChecker
            ship: Ship = server_map.spawn_object(x, max(y - 10, 1), b"ship1")
            team = c.get_team()

            def landed():
                do_spawn(x, y, ship)

            ship.set_on_landed(landed)

        def do_welcome4(x, y):
            c.queue_notify(
                loc.NEW_CLIENT_LANDING.encode(),
                ClientAPI.NOTIFY_MESSAGE_COLOR_BRIGHT)
            server_map.schedule_callback(functools.partial(do_ship, x, y), 2000)

        def do_welcome3():
            x = int(server_map.get_width() * c.get_team().location)
            x = random.randint(x - 8, x + 8)
            y = find_ground_y(x)
            c.force_watch(x, y)
            server_map.schedule_callback(functools.partial(do_welcome4, x, y), 2000)

        def schedule_do_welcome3():
            server_map.schedule_callback(do_welcome3, 2000)

        def do_welcome2():
            c.force_query(WelcomeMessage(c, schedule_do_welcome3))

        def do_welcome1():
            c.queue_notify(
                loc.NEW_CLIENT_WELCOME_FIRST.encode(),
                ClientAPI.NOTIFY_MESSAGE_COLOR_BRIGHT)
            server_map.schedule_callback(do_welcome2, 2000)

        x = server_map.get_width() // 2
        y = find_ground_y(x)

        def f1():
            c.play_playlist()

        def watch():
            c.stop_music()
            server_map.schedule_callback(f1, 500)
            c.force_watch(x, y)

            if sc.respawn:
                server_map.schedule_callback(do_welcome3, 2000)
            else:
                server_map.schedule_callback(do_welcome1, 2000)

        def intro():
            if not c.authenticated:
                # wait till everything has synced proper
                server_map.schedule_callback(intro, 100)
                return

            intro_send_delay = 400

            def switch_state(state: int):
                c.module_action(ClientAPI.MODULE_INTRO, {b"s": state.to_bytes(1, "little")})

            def on_intro2_done():
                c.remove_action_handle(b"done")
                c.remove_action_handle(b"skip")

                server_map.schedule_callback(functools.partial(switch_state, 2), 200)
                server_map.schedule_callback(watch, 1000)

            def on_intro1_done():
                c.handle_action_once(b"done", on_intro2_done)
                server_map.schedule_callback(functools.partial(c.play_music, b"DEMO"), intro_send_delay)
                server_map.schedule_callback(functools.partial(c.push_screen, b"planet"), intro_send_delay * 2)
                server_map.schedule_callback(functools.partial(switch_state, 1), intro_send_delay * 3)

            c.handle_action_once(b"done", on_intro1_done)
            c.handle_action_once(b"skip", on_intro2_done)
            c.push_module(ClientAPI.MODULE_INTRO)
            server_map.schedule_callback(functools.partial(switch_state, 0), intro_send_delay)

        if c.get_team() and c.get_team().fob_placed:
            start = do_welcome3
        elif scenario == b"respawn":
            start = do_welcome3
        else:
            start = intro

        # schedule because c gets assigned after this call
        server_map.schedule_callback(start, 100)
