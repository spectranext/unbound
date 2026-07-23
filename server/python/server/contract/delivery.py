from typing import Dict, List, TYPE_CHECKING

from . import Contract, ContractProgress, TaskDescription
from . events import ContractEvent, ItemDelivered
from .. items import Item
from .. art import SHIP
from .. import loc

if TYPE_CHECKING:
    from .. team import Team


class ItemDeliveryContractProgress(ContractProgress):
    def __init__(self, team: 'Team', c: 'ItemDeliveryContract'):
        super().__init__(team, c)
        self.deliveries: Dict[Item, int] = {}
        self.c = c

    def get_tasks(self) -> List[TaskDescription]:
        if self.team is None:
            return [
                TaskDescription(
                    "Deliver {0} of {1}".format(
                        am, item.name),
                    loc.TASK_DELIVER_STATIC.format(str(am - self.deliveries.get(item, 0)), item.name),
                    item.icon, SHIP)
                for item, am in self.c.requirements.items()
            ]
        return [
            TaskDescription(
                "{0} out of {1} of {2} | {3}%".format(
                    self.deliveries.get(item, 0), am, item.name,
                    str(int(100 * float(self.deliveries.get(item, 0)) / float(am)))),
                loc.TASK_DELIVER.format(str(am - self.deliveries.get(item, 0)), item.name),
                item.icon, SHIP)
            for item, am in self.c.requirements.items()
        ]

    def is_item_relevant(self, item: 'Item'):
        return item in self.c.requirements.keys()

    def get_progress(self) -> int:
        f = 0
        for item, am in self.c.requirements.items():
            f += float(self.deliveries.get(item, 0)) / float(am)
        f /= len(self.c.requirements)
        return int(100 * f)

    def on_event(self, e: ContractEvent) -> bool:
        if not isinstance(e, ItemDelivered):
            return False

        item_to_delete = []
        for item, am in e.items.items():
            if item not in self.c.requirements:
                continue
            have = self.deliveries.get(item, 0)
            need = self.c.requirements.get(item, 0) - have
            if need <= 0:
                continue
            take = min(need, am)
            self.deliveries[item] = have + take
            e.items[item] = am - take
            if am - take == 0:
                item_to_delete.append(item)

        for it in item_to_delete:
            del e.items[it]

        for item, amount in self.c.requirements.items():
            if self.deliveries.get(item, 0) < amount:
                return False

        return True


class ItemDeliveryContract(Contract):

    def __init__(self, contract_id: str, name: str, reward: int, requirements: Dict[Item, int]):
        super().__init__(contract_id, name, reward)
        self.requirements = requirements

    def get_progress_instance(self, team: 'Team') -> ContractProgress:
        return ItemDeliveryContractProgress(team, self)
