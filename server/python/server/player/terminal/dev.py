from typing import TYPE_CHECKING
from . cmd import TerminalError, TerminalCommand

if TYPE_CHECKING:
    from .. client import Client


class Dev(TerminalCommand):
    def apply(self, c: 'Client', *args: str) -> str:
        from .. import items

        if c.player is None:
            raise TerminalError("Not spawned")

        o = c.player

        inventory = o.get_team().inventory
        inventory.add_item(items.get("oxygen"), 4, 1.)
        inventory.add_item(items.get("power_s"), 1, 1.)
        inventory.add_item(items.get("factory_s"), 1, 1.)
        inventory.add_item(items.get("pole_s"), 1, 1.)
        inventory.add_item(items.get("pole_s"), 1, 1.)
        inventory.add_item(items.get("computer_s"), 1, 1.)
        inventory.add_item(items.get("ore_iron"), 20, 1.)
        inventory.add_item(items.get("tube"), 30, 1.)
        inventory.add_item(items.get("rocket"), 20, 1.)
        inventory.add_item(items.get("collector_s"), 1, 1.)
        inventory.add_item(items.get("sh1"), 1, 1.)
        inventory.add_item(items.get("rf1"), 1, 1.)

        return "OK"

    def required_role(self) -> int:
        return 2
