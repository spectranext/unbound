from typing import TYPE_CHECKING
from . cmd import TerminalError, TerminalCommand

if TYPE_CHECKING:
    from .. client import Client


class Report(TerminalCommand):
    def apply(self, c: 'Client', *args: str) -> str:
        if not c.has_team():
            raise TerminalError("No team")
        t = c.get_team()
        if not t:
            raise TerminalError("No team")
        t.generate_weekly_report(c)
        return "OK"
