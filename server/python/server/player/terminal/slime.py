from typing import TYPE_CHECKING
from . cmd import TerminalError, TerminalCommand
from ... api.map import MapAPI

if TYPE_CHECKING:
    from .. client import Client


class Slime(TerminalCommand):
    def apply(self, c: 'Client', *args: str) -> str:
        if c.player is None:
            raise TerminalError("Not spawned")

        MapAPI.instance.spawn_object(c.player.get_x(), c.player.get_y(), b"slime")
        return "OK"

    def minimum_args(self) -> int:
        return 0

    def required_role(self) -> int:
        return 1
