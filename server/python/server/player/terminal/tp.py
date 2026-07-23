from typing import TYPE_CHECKING
from . cmd import TerminalError, TerminalCommand
from ... api.map import MapAPI

if TYPE_CHECKING:
    from .. client import Client


class Teleport(TerminalCommand):
    def apply(self, c: 'Client', *args: str) -> str:
        from .. client import Client

        if c.player is None:
            raise TerminalError("Not spawned")
        try:
            target_player = int(args[0])
        except:
            raise TerminalError("Incorrect target player")

        target = MapAPI.instance.get_client(target_player)
        if not isinstance(target, Client):
            raise TerminalError("No such target player")

        if target.player is None:
            raise TerminalError("Target player is not spawned")

        c.player.set_location(target.player.get_x(), target.player.get_y())
        return "OK"

    def minimum_args(self) -> int:
        return 1

    def required_role(self) -> int:
        return 1
