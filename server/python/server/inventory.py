from typing import Dict, List, Generator, Tuple, Optional, TYPE_CHECKING
import json

if TYPE_CHECKING:
    from . items import Item


class InventoryRecord(object):
    def __init__(self, record_id: int, item: 'Item', amount: int, health: float):
        self.record_id = record_id
        self.item = item
        self.amount = amount
        self.health = health


class Inventory(object):
    def __init__(self):
        self.entries: Dict[int, InventoryRecord] = {}
        self.next_record_id = 0

    def get_records(self, item: 'Item') -> Generator[InventoryRecord, None, None]:
        for v in self.entries.values():
            if v.item == item:
                yield v

    def is_full(self) -> bool:
        return False

    def get_record(self, item: 'Item') -> Optional[InventoryRecord]:
        for v in self.entries.values():
            if v.item == item:
                return v
        return None

    def count_items(self, item: 'Item') -> int:
        count = 0
        for v in self.get_records(item):
            count += v.amount
        return count

    def count_total_items(self) -> int:
        count = 0
        for v in self.entries.values():
            count += v.amount
        return count

    def has_something(self) -> bool:
        return bool(len(self.entries.keys()))

    def clear(self):
        self.entries = {}

    def has_item(self, item: 'Item'):
        return self.get_record(item) is not None

    def move_items(self, items_from: 'Inventory'):
        for k, v in items_from.entries.items():
            self.add_item(v.item, v.amount, v.health)
        items_from.clear()

    def add_item(self, item: 'Item', amount: int, health: float) -> Tuple[int, InventoryRecord]:
        for v in self.get_records(item):
            if v.amount >= item.stack_limit:
                continue
            fits = item.stack_limit - v.amount
            take = min(fits, amount)
            v.health = (v.health * v.amount + health) / (v.amount + take)
            v.amount += take
            amount -= take
            if amount == 0:
                return self.count_items(item), v
        new_record = InventoryRecord(self.next_record_id, item, amount, health)
        self.entries[self.next_record_id] = new_record
        self.next_record_id += 1
        return self.count_items(item), new_record

    def remove_from_record(self, record: InventoryRecord, amount: int) -> int:
        if record.record_id not in self.entries:
            return 0
        if amount > record.amount:
            return 0
        record.amount -= amount
        if record.amount == 0:
            del self.entries[record.record_id]
        return record.amount

    def remove_item(self, item: 'Item', amount: int) -> Tuple[float, int]:
        if amount > self.count_items(item):
            return 0, self.count_items(item)
        health = 0
        taken = 0
        cleanup: List[int] = []
        for v in self.get_records(item):
            if health is None:
                health = v.health
            need = amount - taken
            can = min(v.amount, need)
            health = (health * taken + v.health) / (taken + can)
            v.amount -= can
            if v.amount == 0:
                cleanup.append(v.record_id)
            taken += can
            if taken >= amount:
                break
        for record_id in cleanup:
            del self.entries[record_id]
        return health, self.count_items(item)

    def serialize(self) -> bytes:
        return json.dumps({
            "n": self.next_record_id,
            "r": {
                v.record_id: [v.item.identity, v.amount, v.health]
                for v in self.entries.values()
            }
        }).encode()

    def to_dict(self) -> Dict['Item', int]:
        result = {}
        for v in self.entries.values():
            result[v.item] = result.get(v.item, 0) + v.amount
        return result

    def deserialize(self, data: bytes):
        from . items import Item

        p = json.loads(data.decode())
        self.next_record_id = p["n"]
        self.entries = {}
        for k, v in p["r"].items():
            identity, amount, health = v
            self.entries[int(k)] = InventoryRecord(int(k), Item.ITEMS[identity], amount, health)
