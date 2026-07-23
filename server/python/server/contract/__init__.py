from typing import List, Optional, TYPE_CHECKING, Union
from datetime import datetime, timedelta

from .. api.client import ClientAPI
from . events import ContractEvent
from .. deadline import Deadline
from .. import loc

if TYPE_CHECKING:
    from .. team import Team
    from .. items import Item


class TaskDescription(object):
    def __init__(self, name: str, description: str, icon: Union[int, bytes], image: Optional[bytes]):
        self.name = name
        self.description = description
        if isinstance(icon, bytes):
            self.icon = icon
        else:
            self.icon = icon & 0xFF
        self.image = image


class ContractProgress(object):
    def __init__(self, team: 'Team', contract: 'Contract'):
        from .. api.map import MapAPI
        self.team = team
        self.contract = contract
        self.deadlines = [0.5, 0.45, 0.2, 0.15, 0.1, 0.1, 0.1]
        self.reminder = 0

    def get_tasks(self) -> List[TaskDescription]:
        return []

    def is_item_relevant(self, item: 'Item'):
        return False

    def update(self):
        pass

    def on_event(self, e: ContractEvent) -> bool:
        pass

    def get_progress(self) -> int:
        return 0


class Contract(object):
    def __init__(self, contract_id: str, name: str, reward: int):
        self.contract_id = contract_id
        self.name = name
        self.reward = reward

    def get_progress_instance(self, team: 'Team') -> ContractProgress:
        return ContractProgress(team, self)
