from typing import TYPE_CHECKING, Dict
from . cmd import TerminalError, TerminalCommand
from ... items import Item

if TYPE_CHECKING:
    from .. client import Client


class Give(TerminalCommand):
    KITS: Dict[str, Dict[str, int]] = {
        "factory100": {
            "factory_s": 1,
            "power_s": 1,
            "tube": 40,
            "oxygen": 2
        }
    }
    def apply(self, c: 'Client', *args: str) -> str:
        o = c.get_control_object() or c.get_client_object()
        if o is None:
            raise TerminalError("Not spawned")
        needed_item = args[0]
        if needed_item in Give.KITS:
            for k, v in Give.KITS[needed_item].items():
                o.add_to_inventory(Item.ITEMS[k], v, 1, notify=False)
            return "OK: kit {0}".format(needed_item)

        if needed_item not in Item.ITEMS:
            raise TerminalError("Unknown item: {0}".format(needed_item))
        if len(args) > 1:
            try:
                amt = int(args[1])
            except:
                raise TerminalError("Incorrect amount")
        else:
            amt = 1
        o.add_to_inventory(Item.ITEMS[needed_item], amt, 1, notify=False)
        return "OK: {0}".format(", ".join(args))

    def minimum_args(self) -> int:
        return 1

    def required_role(self) -> int:
        return 1
