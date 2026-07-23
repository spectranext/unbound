from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from .. client import Client


class TerminalError(Exception):
    def __init__(self, s: str):
        self.s = s

    def __str__(self) -> str:
        return self.s


class TerminalCommand(object):
    def apply(self, c: 'Client', *args: str) -> str:
        pass

    def minimum_args(self) -> int:
        return 0

    def required_role(self) -> int:
        return 2
