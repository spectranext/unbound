from typing import TYPE_CHECKING

from . cmd import TerminalCommand, TerminalError
from .. import inventorymodule

if TYPE_CHECKING:
    from .. client import Client


class PlayerInventory(TerminalCommand):
    def apply(self, c: 'Client', *args: str) -> str:
        o = c.get_control_object() or c.get_client_object()
        if o is None:
            raise TerminalError("Not spawned")

        if c.get_control_object() and c.get_control_object() != c.get_client_object():
            inventory = o.get_inventory()
        elif c.has_team():
            inventory = c.get_team().inventory
        else:
            inventory = o.get_inventory()

        if inventory is None:
            raise TerminalError("No inventory")
        inventorymodule.show(c, inventory, "Inventory")
        return "OK"

    def required_role(self) -> int:
        return 1
