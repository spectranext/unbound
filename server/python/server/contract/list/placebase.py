from typing import List, TYPE_CHECKING

from ... contract import Contract, ContractProgress, TaskDescription
from ... import blocks
from .. events import ContractEvent, BasePlaced
from ... import loc
from ... items import Item

if TYPE_CHECKING:
    from server.team import Team


class PlaceBaseProgress(ContractProgress):
    def __init__(self, team: 'Team', contract: 'PlaceBase'):
        super().__init__(team, contract)
        self.base_to_place: Item = Item.ITEMS.get(contract.base_to_place)
        self.name = contract.name
        self.description = contract.description

    def on_event(self, e: ContractEvent) -> bool:
        if isinstance(e, BasePlaced):
            return e.base == self.base_to_place
        return False

    def get_tasks(self) -> List[TaskDescription]:
        return [TaskDescription(
            self.name,
            self.description,
            self.base_to_place.icon, None)]

    def get_progress(self) -> int:
        return 0

    def is_complete(self) -> bool:
        return self.team.bases_placed > 0


class PlaceBase(Contract):
    def __init__(self, contract_id: str, base_to_place: str, reward: int, title: str, name: str, description: str):
        super().__init__(contract_id, title, reward)
        self.base_to_place = base_to_place
        self.name = name
        self.description = description

    def get_progress_instance(self, team: 'Team') -> ContractProgress:
        return PlaceBaseProgress(team, self)
