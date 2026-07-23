from typing import TYPE_CHECKING
from . cmd import TerminalError, TerminalCommand
from ... items import Item

if TYPE_CHECKING:
    from .. client import Client


class Debug(TerminalCommand):
    def apply(self, c: 'Client', *args: str) -> str:
        import pydevd_pycharm
        try:
            pydevd_pycharm.settrace('localhost', port=1379, stdoutToServer=True, stderrToServer=True, suspend=False)
        except Exception as e:
            raise TerminalError(str(e))
        else:
            return "OK"

    def required_role(self) -> int:
        return 2
