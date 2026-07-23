from typing import TYPE_CHECKING
from . cmd import TerminalCommand, TerminalError
from . mark import Mark
from ... bot.bot import Bot

if TYPE_CHECKING:
    from .. client import Client


class Goto(TerminalCommand):
    def apply(self, c: 'Client', *args: str) -> str:
        mark = c.terminal_context.get(Mark.KEY)
        if mark is None:
            raise TerminalError("No mark set")

        bot = c.get_control_object()
        if not isinstance(bot, Bot):
            raise TerminalError("Goto requires controlling a bot")

        if bot.get_client_id() != c.client_id:
            raise TerminalError("Goto requires controlling this bot")

        x, y = mark
        bot.go_location(int(x), int(y))
        bot.release_control_by()
        return "OK: bot moving to mark"

    def required_role(self) -> int:
        return 0
