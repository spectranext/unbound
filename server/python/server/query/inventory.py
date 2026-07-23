from typing import List, Optional

from .. api.query import QueryResponse, QueryResponseOption
from .. api.map import MapAPI
from .. api.object import ObjectAPI
from .. api.client import ClientAPI
from .. inventory import InventoryRecord, Inventory
from .. import loc


class InventoryEntry(QueryResponseOption):
    def __init__(self, record: InventoryRecord):
        self.record = record

    def icon(self):
        return self.record.item.icon

    def __str__(self):
        if self.record.item.stack_limit == 1:
            str_health = str(int(100. * self.record.health)) + '%'
            return "{0}{1}{2}".format(
                self.record.item.name,
                " " * (28 - len(self.record.item.name) - len(str_health)), str_health)

        str_amount = str(self.record.amount)
        return "{0}{1}{2}".format(
            self.record.item.name,
            " " * (28 - len(self.record.item.name) - len(str_amount)), str_amount)


class MoreInfoInventoryQueryResponse(QueryResponse):
    def __init__(self, player: ObjectAPI, record: InventoryRecord):
        super().__init__(b"", record.item.name.encode())
        self.record = record
        self.player = player
        self.description = loc.PLAYER_STATE.format(record.item.description, int(record.health * 100.)).encode()

        if self.record.item.self_action_title:
            self.actions = [self.record.item.self_action_title, loc.OK.encode()]
        else:
            self.actions = [loc.OK.encode()]

    def selected(self, option: int, action: bytes) -> Optional['QueryResponse']:
        if self.record.item.self_action_title and self.record.item.self_action_title == action:
            if self.record.item.self_action(self.player):
                self.player.remove_record_from_inventory(self.record, 1)
            else:
                client_id = self.player.get_client_id()
                if client_id:
                    client = MapAPI.instance.get_client(client_id)
                    if client:
                        client.queue_notify(
                            loc.PLAYER_INVENTORY_CANT_USE.format(self.record.item.self_action_title.decode()).encode(),
                            ClientAPI.NOTIFY_MESSAGE_COLOR_DANGER, priority=True)
        return None


class InventoryQueryResponse(QueryResponse):
    def __init__(self, query: bytes, player: ObjectAPI, inventory: Inventory, title: str):
        self.player = player
        self.inventory = inventory
        super().__init__(query, title.encode())

        self.options: List[InventoryEntry] = []

        if inventory.has_something():
            self.empty_inventory = False
            client = self.player.get_client()
            selected_record = client.get_selected_record() if client else None
            for value in inventory.entries.values():
                if selected_record == value:
                    self.current = len(self.options)
                self.options.append(InventoryEntry(value))
            self.actions = [loc.SEE_MORE.encode(), loc.SELECT.encode()]
        else:
            self.empty_inventory = True
            self.description = loc.PLAYER_INVENTORY_EMPTY.encode()
            self.actions = [loc.EXIT.encode()]

    def selected(self, option: int, action: bytes):
        if self.empty_inventory:
            return None
        MapAPI.instance.print("Player {0} selected action {1} on option {2}".format(
            self.player.get_client_id(), action, self.options[option]
        ))
        if action == loc.SEE_MORE.encode():
            if option >= len(self.options):
                return None
            itm = self.options[option]
            return MoreInfoInventoryQueryResponse(self.player, itm.record)
        if action == loc.SELECT.encode():
            if option >= len(self.options):
                return None
            client = MapAPI.instance.get_client(self.player.get_client_id())
            if client:
                client.select_record(self.options[option].record, self.inventory)
        return None
