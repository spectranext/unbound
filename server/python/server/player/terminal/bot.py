from typing import TYPE_CHECKING
from . cmd import TerminalError, TerminalCommand
from ... api.map import MapAPI
from ... bot.bot import Bot

if TYPE_CHECKING:
    from .. client import Client


class Bot(TerminalCommand):
    def apply(self, c: 'Client', *args: str) -> str:
        player = c.get_client_object()
        if not player:
            raise TerminalError("Not spawned")

        o = MapAPI.instance.spawn_object(player.get_x(), player.get_y(), b"bot")
        o.set_team(player.get_team())
        return "OK"

    def minimum_args(self) -> int:
        return 0

    def required_role(self) -> int:
        return 1
