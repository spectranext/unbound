from typing import TYPE_CHECKING

from .. inventory import Inventory

if TYPE_CHECKING:
    from . client import Client


MODULE_NAME = b"INVENTORY"


def _clip_title(title: str) -> bytes:
    return title[:12].encode()


def _send_inventory(client: 'Client', inventory: Inventory, panel: int = 0):
    for item, amount in inventory.to_dict().items():
        payload = {
            b"i": str(item.pure_icon()).encode(),
            b"c": str(amount).encode(),
        }
        if panel:
            payload[b"p"] = str(panel).encode()
        client.module_action(MODULE_NAME, payload)


def show(client: 'Client', inventory: Inventory, title: str):
    client.push_module(MODULE_NAME)
    client.module_action(MODULE_NAME, {b"l": _clip_title(title)})
    _send_inventory(client, inventory)


def show_double(client: 'Client', left: Inventory, left_title: str, right: Inventory, right_title: str):
    client.push_module(MODULE_NAME)
    client.module_action(MODULE_NAME, {
        b"l": _clip_title(left_title),
        b"r": _clip_title(right_title),
    })
    _send_inventory(client, left)
    _send_inventory(client, right, 1)
