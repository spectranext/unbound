from typing import TYPE_CHECKING
from . cmd import TerminalError, TerminalCommand
from ... api.map import MapAPI

if TYPE_CHECKING:
    from .. client import Client


class Temperature(TerminalCommand):
    def apply(self, c: 'Client', *args: str) -> str:
        if c.player is None:
            raise TerminalError("Not spawned")

        if len(args) > 0:
            try:

                amt = int(args[0])
            except:
                raise TerminalError("Incorrect amount")
        else:
            amt = 0

        c.player.temperature = amt
        c.sync_stats()
        return "OK"

    def minimum_args(self) -> int:
        return 0

    def required_role(self) -> int:
        return 0
