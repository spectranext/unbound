from typing import TYPE_CHECKING
from . cmd import TerminalError, TerminalCommand

if TYPE_CHECKING:
    from .. client import Client


class CPU(TerminalCommand):
    def apply(self, c: 'Client', *args: str) -> str:
        from ... api.map import MapAPI

        if c.player is None:
            raise TerminalError("Not spawned")

        cpu = MapAPI.instance.computer_new(c.get_team().team_id, None)

        autoboot = False
        if len(args) > 0:
            autoboot = args[0] == "true"

        cpu.set_tnfs(c.get_team().tnfs, autoboot)

        cpu.set_power(True)

        def start():
            c.computer_session(cpu, cpu_done, info=False)

        def cpu_done():
            cpu.destroy()

        MapAPI.instance.schedule_callback(start, 2000)
        return "OK"

    def required_role(self) -> int:
        return 2
