import os
from typing import Optional, Union
from datetime import timedelta
from urllib.parse import urlencode, urlparse, urlunparse

from .. api.map import MapAPI
from .. api.query import QueryResponse, QueryResponseOption, OPT, NOACT
from . import PlayerObject
from . map import MapQueryResponse
from . music import MusicQueryResponse
from . tutorial import TUTORIAL_DIALOGS
from . email import EmailSession
from .. art import CONTRACT, STATUS, FULL_CONTRACT, FULL_DEADLINE, SUN
from .. imagegen import Image
from .. contract import TaskDescription
from .. import loc, icons
from .. team import DEBT_INCREMENTS
from .. tuning import Tuning
from . wiki import WikiQueryResponse
from .. qr import qr_image


def append_url_query(url: str, values: dict) -> str:
    parsed = urlparse(url)
    extra = urlencode(values)
    query = f"{parsed.query}&{extra}" if parsed.query else extra
    return urlunparse(parsed._replace(query=query))


class StatusReportQueryResponse(QueryResponse):
    def __init__(self, p: PlayerObject):
        super().__init__(b"", loc.STATUS_REPORT.encode())
        items_count = 0
        for i in p.get_team().inventory.entries.values():
            items_count += i.amount
        self.options = [
            OPT(loc.STATUS_REPORT_SUMMARY_CREDITS.format(p.client.get_team().credits), cb=None, icon=icons.ICON_CREDITS),
            OPT(loc.STATUS_REPORT_SUMMARY_HEALTH.format(p.health), cb=None, icon=icons.ICON_HEALTH),
            OPT(loc.STATUS_REPORT_SUMMARY_OXYGEN.format(str(timedelta(seconds=int(p.power * p.power_consumption))), p.power), cb=None, icon=icons.ICON_OXYGEN),
            OPT(loc.STATUS_REPORT_SUMMARY_TEMPERATURE.format(p.temperature), cb=None, icon=icons.ICON_TEMPERATURE),
        ]

        if p.heat_time > 0:
            self.options.extend([
                OPT(loc.STATUS_REPORT_SUMMARY_HEAT_APPLIED.format(p.heat_time), cb=None, icon=icons.ICON_TEMPERATURE_BOOST)
            ])

        if MapAPI.instance.is_cold():
            self.options.extend([
                OPT(loc.STATUS_REPORT_SUMMARY_COLD_OUTSIDE, cb=None, icon=icons.ICON_TEMPERATURE_DROP)
            ])

        self.options.extend([
            OPT(loc.STATUS_REPORT_SUMMARY_INVENTORY.format(items_count), cb=None, icon=icons.ICON_INVENTORY),
        ])
        self.actions = [loc.OK.encode()]


class PlayersQueryResponse(QueryResponse):
    def __init__(self, p: PlayerObject):
        super().__init__(b"", loc.STATUS_REPORT_PLAYERS.encode())
        self.options = [
            OPT("{0} | {1}".format(p.get_name().decode(), p.get_team().name))
            for p in sorted(MapAPI.instance.query_clients(), key=lambda x: x.get_team().name)
            if p.has_team()
        ]
        self.actions = [loc.OK.encode()]


class HelpQueryResponse(QueryResponse):
    def __init__(self, p: PlayerObject):
        super().__init__(b"", loc.STATUS_REPORT_HELP.encode())
        if not p.client.get_team().fob_placed:
            self.description = loc.STATUS_REPORT_PLACE_FOB.encode()
        else:
            self.description = loc.STATUS_REPORT_HELP_DESC.encode()
        self.actions = [loc.OK.encode()]


class RestartGameConfirmationQueryResponse(QueryResponse):
    def __init__(self, p: PlayerObject):
        super().__init__(b"", loc.STATUS_REPORT_RESTART_GAME_CONFIRM.encode())
        self.player = p
        self.description = loc.STATUS_REPORT_RESTART_GAME_DESC.encode()
        self.actions = [loc.OK.encode(), loc.CONFIRM.encode()]

    def selected(self, option: int, action: bytes) -> Optional['QueryResponse']:
        if action == loc.CONFIRM.encode():
            self.restart_game()
        return None

    def restart_game(self):
        MapAPI.instance.shutdown()


class WebPortalQueryResponse(QueryResponse):
    def __init__(self, p: PlayerObject):
        super().__init__(b"", loc.STATUS_REPORT_WEB_PORTAL.encode())
        url = self.web_portal_url(p)
        self.description = b"Scan this QR code to open your team's web portal."
        self.image = qr_image(url).bake()
        self.actions = [loc.OK.encode()]

    @staticmethod
    def web_portal_url(p: PlayerObject) -> str:
        from .. http.rpc import compute_server_hash, get_report_address, report_web_url
        from .. http.web_actions.player import decode_bytes, player_context

        report_url = os.environ["REPORT_URL"]
        server_hash = os.environ.get("REPORT_SERVER_HASH")
        if not server_hash:
            port = os.environ["REPORT_PORT"]
            server_hash = compute_server_hash(get_report_address(), port)

        team = p.client.get_team()
        url = report_web_url(
            report_url,
            server_hash,
            "team",
            decode_bytes(p.client.get_name()),
            context=player_context(p.client, include_team=True),
        )
        return append_url_query(url, {"team_id": team.team_id})


class StatusMoreQueryResponse(QueryResponse):
    def __init__(self, p: PlayerObject):
        super().__init__(b"", loc.STATUS_REPORT_MORE.encode())
        self.options = [
            OPT(loc.STATUS_REPORT_PLAYERS_ONLINE.format(len(MapAPI.instance.query_clients())), NOACT(PlayersQueryResponse, p), icon=icons.ICON_PLAYERS_ONLINE),
            OPT(loc.STATUS_REPORT_HELP, NOACT(HelpQueryResponse, p), icon=icons.ICON_HELP),
            OPT(loc.STATUS_REPORT_WIKI, NOACT(WikiQueryResponse, p), icon=icons.ICON_WIKI),
        ]
        if (
            p.client.get_team() is not None
            and os.environ.get("REPORT_URL")
            and (os.environ.get("REPORT_SERVER_HASH") or os.environ.get("REPORT_PORT"))
        ):
            self.options.append(
                OPT(loc.STATUS_REPORT_WEB_PORTAL, NOACT(WebPortalQueryResponse, p), icon=icons.ICON_WEB)
            )
        self.options.extend([
            OPT(loc.STATUS_REPORT_RESTART_GAME, NOACT(RestartGameConfirmationQueryResponse, p), icon=icons.ICON_ERROR),
            Tutorial(p),
        ])
        self.actions = [loc.OK.encode()]


class ContractTaskResponse(QueryResponse):
    def __init__(self, task: TaskDescription):
        super().__init__(b"", task.name.encode(), task.image)
        self.description = task.description.encode()
        self.actions = [loc.OK.encode()]


class ContractQueryResponse(QueryResponse):
    def __init__(self, p: PlayerObject):
        super().__init__(b"",
                         loc.CONTRACT_STATUS.format(p.client.get_team().contract_progress.get_progress()).encode()
                         if p.client.get_team().contract_progress else loc.CONTRACT.encode(), CONTRACT)
        if p.client.get_team().current_contract:
            self.description = loc.CONTRACT_CONDITIONS.format(
                p.client.get_team().current_contract.reward).encode()
            self.options = [
                OPT(task.name, NOACT(ContractTaskResponse, task), task.icon)
                for task in p.client.get_team().contract_progress.get_tasks()
            ]
        self.actions = [loc.OK.encode()]


class BankRepayAmountOption(QueryResponseOption):
    def __init__(self, player: PlayerObject, amount: int):
        self.player = player
        self.amount = amount

    def __str__(self):
        return loc.BANK_AMOUNT.format(self.amount)

    def icon(self):
        return icons.ICON_CREDITS

    def act(self, action: bytes) -> Optional['QueryResponse']:
        if self.player.get_team().debt.repay(self.amount):
            return None
        return BankAccountQueryResponse(self.player)


class BankBorrowAmountOption(QueryResponseOption):
    def __init__(self, player: PlayerObject, amount: int):
        self.player = player
        self.amount = amount

    def __str__(self):
        return loc.BANK_AMOUNT.format(self.amount)

    def icon(self):
        return icons.ICON_CREDITS

    def act(self, action: bytes) -> Optional['QueryResponse']:
        if self.player.get_team().debt.borrow(self.amount):
            return None
        return BankAccountQueryResponse(self.player)


class BankRepayQueryResponse(QueryResponse):
    def __init__(self, p: PlayerObject):
        team = p.get_team()
        max_repay = min(team.credits, team.debt.remaining)
        super().__init__(b"", loc.BANK_REPAY.encode())
        self.options = [
            BankRepayAmountOption(p, amount)
            for amount in DEBT_INCREMENTS
            if amount <= max_repay
        ]
        self.actions = [loc.OK.encode()]


class BankBorrowQueryResponse(QueryResponse):
    def __init__(self, p: PlayerObject):
        team = p.get_team()
        capacity = team.debt.borrowing_capacity()
        super().__init__(b"", loc.BANK_BORROW.encode())
        self.options = [
            BankBorrowAmountOption(p, amount)
            for amount in DEBT_INCREMENTS
            if amount <= capacity
        ]
        self.actions = [loc.OK.encode()]


class BankAccountQueryResponse(QueryResponse):
    def __init__(self, p: PlayerObject):
        team = p.get_team()
        debt = team.debt
        super().__init__(b"", loc.BANK_ACCOUNT.encode())
        self.description = loc.BANK_ACCOUNT_DESC.format(
            team.credits,
            debt.remaining,
            int(Tuning.DEBT_WEEKLY_RATE * 100),
            debt.weekly_payment(),
            Tuning.DEBT_CEILING,
        ).encode()
        self.options = [
            OPT(loc.BANK_REPAY, NOACT(BankRepayQueryResponse, p), icon=icons.ICON_CREDITS),
            OPT(loc.BANK_BORROW, NOACT(BankBorrowQueryResponse, p), icon=icons.ICON_CREDITS),
        ]
        self.actions = [loc.OK.encode()]


class Tutorial(QueryResponseOption):
    def __init__(self, p: PlayerObject):
        self.p = p

    def __str__(self):
        return loc.TUTORIAL

    def icon(self) -> Union[int, bytes]:
        return icons.ICON_TUTORIAL

    def act(self, action: bytes) -> Optional['QueryResponse']:
        self.p.client.tutorial = False
        return StatusQueryResponse(b"", self.p)


class StatusQueryResponse(QueryResponse):

    FULL_CONTRACT_IMAGE = Image(source=FULL_CONTRACT)
    FULL_DEADLINE_IMAGE = Image(source=FULL_DEADLINE)
    SUN = Image(source=SUN)

    def __init__(self, q: bytes, p: PlayerObject):
        from .. crafting import CraftingQueryResponse

        super().__init__(q, loc.STATUS_REPORT_TITLE.format(
            p.client.get_team().credits,
            MapAPI.instance.get_phase_name(),
            MapAPI.instance.get_time_hours(),
            MapAPI.instance.get_time_minutes()).encode())
        self.player = p
        self.dialog = 0
        if not p.client.tutorial:
            p.client.spawn_tutorial_god = True
            self.tutorial_mode = True
            self.update_tutorial()
            self.actions = [loc.PROCEED.encode()]
        else:
            self.tutorial_mode = False

            progress = p.client.get_team().contract_progress.get_progress()

            im = Image(source=STATUS)

            health = int(46 * (p.health / 100))
            im.fill(17, 11, health, 2)

            power = int(46 * (p.power / 100))
            im.fill(17, 19, power, 2)

            phase = int(12 * MapAPI.instance.get_phase())
            im.place_image(StatusQueryResponse.SUN, 96 - phase * 16, 8, phase * 16, 0, phase * 16 + 16, 16)

            if progress:
                h = int(16 * (progress / 100))
                im.place_image(StatusQueryResponse.FULL_CONTRACT_IMAGE, 72, 8, 0, 16 - h)

            self.image = im.bake()

            self.options = [
                OPT(loc.STATUS_EMAIL, NOACT(self.open_email), icon=icons.ICON_EMAIL_READ if p.client.email_inbox.count_unread() == 0 else icons.ICON_EMAIL),
                OPT(loc.STATUS_REPORT, NOACT(StatusReportQueryResponse, p), icon=icons.ICON_STATUS_REPORT),
                OPT(loc.BANK_ACCOUNT, NOACT(BankAccountQueryResponse, p), icon=icons.ICON_CREDITS),
                OPT(loc.CRAFTING, NOACT(CraftingQueryResponse, b"", p), icons.ICON_CRAFTING),
                OPT(loc.STATUS_REPORT_CONTRACTS_PROGRESS.format(progress)
                    if p.client.get_team().contract_progress else loc.STATUS_REPORT_CONTRACTS,
                    NOACT(ContractQueryResponse, p), icon=icons.ICON_CONTRACT),
                OPT(loc.STATUS_REPORT_MAP, NOACT(MapQueryResponse, p), icon=icons.ICON_MAP),
                OPT(loc.STATUS_REPORT_MUSIC.format(p.client.get_current_music_name()), NOACT(MusicQueryResponse, p), icon=icons.ICON_MUSIC),
                OPT(loc.STATUS_REPORT_MORE, NOACT(StatusMoreQueryResponse, p), icon=icons.ICON_HELP),
            ]
            self.actions = [loc.OK.encode()]

    def open_email(self, *args, **kwargs) -> QueryResponse:
        return EmailSession(self.player.client)

    def update_tutorial(self):
        self.image, self.message, self.description = TUTORIAL_DIALOGS[self.dialog]

    def selected(self, option: int, action: bytes) -> Optional['QueryResponse']:
        if self.tutorial_mode:
            if self.dialog < len(TUTORIAL_DIALOGS) - 1:
                self.dialog += 1
                self.update_tutorial()
                return self
            else:
                self.player.client.tutorial = True
                self.player.client.spawn_tutorial_god = False
                return None
        return super().selected(option, action)

    def cancelled(self) -> Optional['QueryResponse']:
        self.player.client.spawn_tutorial_god = False
        return None
