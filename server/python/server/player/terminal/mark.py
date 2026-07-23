from typing import TYPE_CHECKING
from . cmd import TerminalCommand, TerminalError

if TYPE_CHECKING:
    from .. client import Client


class Mark(TerminalCommand):
    KEY = "mark"

    def apply(self, c: 'Client', *args: str) -> str:
        player = c.get_client_object()
        if player is None:
            raise TerminalError("Not spawned")

        c.terminal_context[Mark.KEY] = (int(player.get_x()), int(player.get_y()))
        return "OK: mark set"

    def required_role(self) -> int:
        return 0
