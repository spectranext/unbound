import os
import pickle
from typing import List, Tuple, Optional, Dict
import functools
import math

from . api.client import ClientAPI
from . api.map import MapAPI
from . inventory import Inventory
from . tuning import Tuning
from . contract import Contract
from . contract.events import ContractEvent
from . import loc, icons
from . loc import emails
from . art import OPERATIONAL_FUNDS
from . imagegen import Image


DEBT_INCREMENTS = [1000, 2000, 5000, 10000, 20000]


class Debt(object):
    def __init__(self, team: 'Team', remaining: int = None, payment_accumulator: float = 0.0):
        self.team = team
        self.remaining = remaining if remaining is not None else Tuning.INITIAL_DEBT
        self.payment_accumulator = payment_accumulator

    def weekly_payment(self) -> int:
        return int(self.remaining * Tuning.DEBT_WEEKLY_RATE)

    def borrowing_capacity(self) -> int:
        return max(0, Tuning.DEBT_CEILING - self.remaining)

    def _notify_members(self, template, **kwargs):
        for member in self.team.members:
            member.add_email_template(template, **kwargs)

    def borrow(self, amount: int) -> bool:
        if amount <= 0:
            return False
        if self.remaining + amount > Tuning.DEBT_CEILING:
            return False
        self.remaining += amount
        self.team.add_credits(amount)
        self._notify_members(
            emails.DEBT_BORROWED,
            amount=amount,
            credits=self.team.credits,
            remaining=self.remaining,
        )
        return True

    def repay(self, amount: int) -> bool:
        if amount <= 0:
            return False
        amount = min(amount, self.remaining, self.team.credits)
        if amount <= 0:
            return False
        self.team.remove_credits(amount)
        self.remaining -= amount
        self._notify_members(
            emails.DEBT_REPAID,
            amount=amount,
            credits=self.team.credits,
            remaining=self.remaining,
        )
        return True

    def cycle(self):
        if self.remaining <= 0:
            return
        weekly_payment = self.remaining * Tuning.DEBT_WEEKLY_RATE
        per_tick = weekly_payment / Team.CREDIT_REPORT_ITERATION
        self.payment_accumulator += per_tick
        while self.payment_accumulator >= 1.0 and self.remaining > 0:
            payment = int(self.payment_accumulator)
            self.payment_accumulator -= payment
            actual = self.team.remove_credits(min(payment, self.remaining))
            self.remaining -= actual
            if actual == 0:
                break


class Team(object):
    NEW_POINT_DISTANCE = 2
    MOVEMENTS_DEPTH = 64
    CREDIT_HISTORY_ITERATION = 24
    CREDIT_REPORT_ITERATION = 672 # weekly
    CREDIT_REPORT_LENGTH = 24

    def __init__(self, name: str, location: float, icon: bytes, team_id: int):
        from . contract import Contract, ContractProgress

        self.name = name
        self.team_id = team_id
        self.location = location
        self.icon = icon
        self.bases_placed = 0
        self.members: List[ClientAPI] = []
        self.credits: int = Tuning.INITIAL_CREDITS
        self.current_contract: Optional[Contract] = None
        self.completed_contracts: List[Contract] = []
        self.contract_progress: Optional[ContractProgress] = None
        self.fob_placed: bool = False
        self.cnt = Team.CREDIT_HISTORY_ITERATION
        self.credits_history: List[int] = [0] * Team.CREDIT_REPORT_LENGTH
        self.added_credits_history: List[int] = [0] * Team.CREDIT_REPORT_LENGTH
        self.removed_credits_history: List[int] = [0] * Team.CREDIT_REPORT_LENGTH
        self.added_credits = 0
        self.removed_credits = 0
        self.week = 1
        self.inventory = Inventory()
        self.bot_delivered: bool = False
        self.financial_cycle_started: bool = False
        self.debt = Debt(self)

    def init(self):
        from . contract.list import CONTRACTS

        self.set_contract(CONTRACTS[0])

    def add_credits(self, c: int):
        self.credits += c
        self.added_credits += c

    def remove_credits(self, c: int) -> int:
        actual = min(c, max(0, self.credits))
        self.credits -= actual
        self.removed_credits += actual
        return actual

    def add_member(self, m: ClientAPI):
        self.financial_cycle_started = True
        m.set_team(self)
        self.members.append(m)
        if self.current_contract:
            self.notify_contract(m)

        MapAPI.instance.schedule_callback(functools.partial(self.generate_weekly_report, target=m), 5000)

    def notify_contract(self, m: ClientAPI):
        objectives = ""

        for task in self.contract_progress.get_tasks():
            objectives += "- {0}\n".format(task.description)

        def deliver():
            m.add_email_template(emails.NEW_CONTRACT,
                contract_name=self.current_contract.name,
                objectives=objectives,
                payment=str(self.current_contract.reward))

        MapAPI.instance.schedule_callback(deliver, 2500)

    def is_star_store_closed(self) -> bool:
        return False

    def generate_weekly_report(self, target: ClientAPI = None):

        img = Image(OPERATIONAL_FUNDS)

        max_v = max(self.credits_history)
        added_sum = sum(self.added_credits_history)
        removed_sum = sum(self.removed_credits_history)

        idx = 0
        for x1 in range(0, 96, 4):
            if idx < len(self.credits_history):
                if max_v != 0:
                    v = self.credits_history[idx]
                    v = 1 + int((v / max_v) * 31)
                else:
                    v = 1
            else:
                v = 1
            idx += 1
            for y1 in range(0, 32 - v):
                img.set_pixel(x1 + 9, y1 + 8, False)
                img.set_pixel(x1 + 10, y1 + 8, False)
            for y1 in range(0, 32):
                img.set_pixel(x1 + 8, y1 + 8, False)
                img.set_pixel(x1 + 11, y1 + 8, False)

        kwargs = {
            "custom_image": img.bake(),
            "week": self.week,
            "quarter_high": max_v,
            "total_revenue": added_sum,
            "total_expenses": removed_sum,
            "net_profit": added_sum - removed_sum,
            "credit_balance": self.credits,
            "debt_remaining": self.debt.remaining,
            "debt_weekly_rate_pct": int(Tuning.DEBT_WEEKLY_RATE * 100),
            "debt_weekly_payment": self.debt.weekly_payment(),
            "debt_ceiling": Tuning.DEBT_CEILING,
            "debt_borrowing_capacity": self.debt.borrowing_capacity(),
        }

        if target:
            target.add_email_template(emails.WEEKLY_REPORT, **kwargs)
        else:
            for c in self.members:
                c.add_email_template(emails.WEEKLY_REPORT, **kwargs)

            self.week += 1

    def update(self):
        if self.financial_cycle_started:
            self.debt.cycle()

            self.cnt += 1
            if self.cnt % Team.CREDIT_HISTORY_ITERATION == 0:
                self.credits_history.append(self.credits)
                self.added_credits_history.append(self.added_credits)
                self.removed_credits_history.append(self.removed_credits)
                while len(self.credits_history) > Team.CREDIT_REPORT_LENGTH:
                    self.credits_history.pop(0)
                while len(self.added_credits_history) > Team.CREDIT_REPORT_LENGTH:
                    self.added_credits_history.pop(0)
                while len(self.removed_credits_history) > Team.CREDIT_REPORT_LENGTH:
                    self.removed_credits_history.pop(0)
            if self.cnt % Team.CREDIT_REPORT_ITERATION == 0:
                self.generate_weekly_report()

        if self.contract_progress:
            self.contract_progress.update()

    def remove_member(self, m: ClientAPI):
        self.members.remove(m)

    def set_contract(self, c: Optional[Contract]):
        self.current_contract = c
        if c:
            self.contract_progress = c.get_progress_instance(self)
            for m in self.members:
                self.notify_contract(m)
        else:
            self.contract_progress = None

    def notify(self, message: str, color: int):
        for m in self.members:
            m.queue_notify(message.encode(), color)

    def notify_concluded_contract(self, c: Contract):
        from . contract.list import CONTRACTS
        from . imagegen import Image
        from . items import Item
        from . api import MapAPI
        from . scenarios import get_scenario

        sc = get_scenario(MapAPI.instance.scenario)

        self.completed_contracts.append(c)
        for m in self.members:
            if not sc.reward_screen:
                m.queue_notify("Contract {0} has been fulfilled".format(c.name).encode(),
                               ClientAPI.NOTIFY_MESSAGE_COLOR_BRIGHT)
                continue

            congrats: List[bytes] = [
                b"Congratulations!",
                "Contract {0} has been fulfilled".format(c.name).encode()
            ]

            stats_m: List[Tuple[bytes, int, int]] = [
                ("Contract: {0}".format(c.name).encode(), Image.INK_WHITE | Image.BRIGHT | Image.PAPER_BLACK, 0),
                (b"Reward:", Image.INK_WHITE | Image.PAPER_BLACK, 1),
                ("       + {0}".format(c.reward).encode(), Image.INK_GREEN | Image.BRIGHT | Image.PAPER_BLACK, 0),
            ]

            first = True
            for key, b in Item.ITEMS.items():
                sp = b.get_store_pricing()
                if not sp:
                    continue
                if not sp.purchasable():
                    continue
                if b.unlock_contract == c:
                    if first:
                        stats_m.append(
                            (b"New Content (StarStore):", Image.INK_WHITE | Image.BRIGHT | Image.PAPER_BLACK, 1))
                    stats_m.append(
                        ("       {0}".format(b.name).encode(),
                         Image.INK_MAGENTA | Image.BRIGHT | Image.PAPER_BLACK, 0))
                    first = False

            first = True
            for key, b in Item.ITEMS.items():
                if not b.get_crafting_recipies():
                    continue
                if b.unlock_contract == c:
                    if first:
                        stats_m.append(
                            (b"New Content (Crafting):", Image.INK_WHITE | Image.BRIGHT | Image.PAPER_BLACK, 1))
                    stats_m.append(
                        ("       {0}".format(b.name).encode(),
                         Image.INK_MAGENTA | Image.BRIGHT | Image.PAPER_BLACK, 0))
                    first = False

            concluded_idx = CONTRACTS.index(c)
            if concluded_idx < len(CONTRACTS) - 1:
                next_contract = CONTRACTS[concluded_idx + 1]
                progress = next_contract.get_progress_instance(self)
                stats_m.extend([
                    ("Next Contract: {0}".format(next_contract.name).encode(),
                     Image.INK_WHITE | Image.BRIGHT | Image.PAPER_BLACK, 1),
                    (b"Conditions:", Image.INK_WHITE | Image.PAPER_BLACK, 0),
                ])

                for t in progress.get_tasks():
                    d = t.description
                    first = True
                    while len(d) >= 58:
                        c = 58
                        while d[c] != ' ':
                            c -= 1
                            if c == 0:
                                c = 58
                                break
                        stats_m.extend([
                            ("{0}{1}".format(" -> " if first else "    ", d[:c]).encode(),
                             Image.INK_WHITE | Image.BRIGHT | Image.PAPER_BLACK, 1 if first else 0)
                        ])
                        first = False
                        d = d[c:]
                    stats_m.extend([
                        ("    {0}".format(d).encode(),
                         Image.INK_WHITE | Image.BRIGHT | Image.PAPER_BLACK, 1 if first else 0)
                    ])

            m.congratulate(congrats, stats_m)

    def contract_event(self, e: ContractEvent):
        from . contract.list import CONTRACTS
        from . api import MapAPI

        if self.contract_progress is None:
            return
        if self.contract_progress.on_event(e):
            self.add_credits(self.current_contract.reward)
            MapAPI.instance.schedule_callback(
                functools.partial(self.notify_concluded_contract, self.current_contract), 2000)
            index = CONTRACTS.index(self.current_contract)
            if index < len(CONTRACTS) - 1:
                self.set_contract(CONTRACTS[index + 1])
            else:
                self.set_contract(None)

    def serialize(self) -> Dict[bytes, bytes]:
        from . contract.list import CONTRACTS
        
        result: Dict[bytes, bytes] = {
            b"name": self.name.encode(),
            b"credits": self.credits.to_bytes(4, "little", signed=True),
            b"inventory": self.inventory.serialize(),
            b"fob_placed": self.fob_placed.to_bytes(1, "little"),
            b"bases_placed": self.bases_placed.to_bytes(4, "little"),
            b"week": self.week.to_bytes(4, "little"),
            b"cnt": self.cnt.to_bytes(4, "little"),
            b"added_credits": self.added_credits.to_bytes(4, "little", signed=True),
            b"removed_credits": self.removed_credits.to_bytes(4, "little", signed=True),
            b"credits_history": pickle.dumps(self.credits_history),
            b"added_credits_history": pickle.dumps(self.added_credits_history),
            b"removed_credits_history": pickle.dumps(self.removed_credits_history),
            b"bot_delivered": self.bot_delivered.to_bytes(1, "little"),
            b"financial_cycle_started": self.financial_cycle_started.to_bytes(1, "little"),
            b"debt_remaining": self.debt.remaining.to_bytes(4, "little", signed=True),
            b"debt_payment_accumulator": pickle.dumps(self.debt.payment_accumulator),
        }
        
        if self.current_contract:
            result[b"current_contract_id"] = self.current_contract.contract_id.encode()
            if self.contract_progress:
                result[b"contract_progress"] = self.serialize_contract_progress()
        
        completed_ids = [c.contract_id.encode() for c in self.completed_contracts]
        result[b"completed_contracts"] = pickle.dumps(completed_ids)
        
        return result

    def serialize_contract_progress(self) -> bytes:
        from . contract.delivery import ItemDeliveryContractProgress
        
        if isinstance(self.contract_progress, ItemDeliveryContractProgress):
            # Serialize the deliveries dict
            deliveries_dict = {
                item.identity.encode(): amount.to_bytes(4, "little")
                for item, amount in self.contract_progress.deliveries.items()
            }
            return pickle.dumps(deliveries_dict)
        return pickle.dumps({})

    def deserialize(self, data: Dict[bytes, bytes]):
        from . contract.list import CONTRACTS_BY_ID
        
        self.credits = int.from_bytes(data[b"credits"], "little", signed=True)
        self.inventory.deserialize(data[b"inventory"])
        self.fob_placed = bool.from_bytes(data[b"fob_placed"], "little")
        self.bases_placed = int.from_bytes(data[b"bases_placed"], "little")
        self.week = int.from_bytes(data[b"week"], "little")
        self.cnt = int.from_bytes(data[b"cnt"], "little")
        self.added_credits = int.from_bytes(data[b"added_credits"], "little", signed=True)
        self.removed_credits = int.from_bytes(data[b"removed_credits"], "little", signed=True)
        self.credits_history = pickle.loads(data[b"credits_history"])
        self.added_credits_history = pickle.loads(data[b"added_credits_history"])
        self.removed_credits_history = pickle.loads(data[b"removed_credits_history"])
        if b"bot_delivered" in data:
            self.bot_delivered = bool.from_bytes(data[b"bot_delivered"], "little")
        if b"financial_cycle_started" in data:
            self.financial_cycle_started = bool.from_bytes(data[b"financial_cycle_started"], "little")
        if b"debt_remaining" in data:
            self.debt.remaining = int.from_bytes(data[b"debt_remaining"], "little", signed=True)
            self.debt.payment_accumulator = pickle.loads(data[b"debt_payment_accumulator"])
        else:
            self.debt.remaining = Tuning.INITIAL_DEBT
            self.debt.payment_accumulator = 0.0

        if b"current_contract_id" in data:
            contract_id = data[b"current_contract_id"].decode()
            if contract_id in CONTRACTS_BY_ID:
                contract = CONTRACTS_BY_ID[contract_id]
                self.set_contract(contract)
                if self.contract_progress and b"contract_progress" in data:
                    self.deserialize_contract_progress(data[b"contract_progress"])
        
        completed_ids = pickle.loads(data[b"completed_contracts"])
        self.completed_contracts = [
            CONTRACTS_BY_ID[cid.decode()]
            for cid in completed_ids
            if cid.decode() in CONTRACTS_BY_ID
        ]

    def deserialize_contract_progress(self, data: bytes):
        from . contract.delivery import ItemDeliveryContractProgress
        from . items import Item
        
        if isinstance(self.contract_progress, ItemDeliveryContractProgress):
            deliveries_dict = pickle.loads(data)
            self.contract_progress.deliveries = {
                Item.ITEMS[item_id.decode()]: int.from_bytes(amount, "little")
                for item_id, amount in deliveries_dict.items()
                if item_id.decode() in Item.ITEMS
            }


def find_team(identity: bytes) -> Optional[Team]:
    d = identity.decode()
    for tm in TEAMS:
        if tm.name == d:
            return tm
    return None


DEV_TNFS = os.environ.get("DEV_TNFS", None)

TEAM_A = Team(loc.TEAM_COALITION, 0.25, icons.ICON_TEAM_1, 1)
TEAM_B = Team(loc.TEAM_RONIN, 0.75, icons.ICON_TEAM_2, 2)
TEAMS: List[Team] = [TEAM_A, TEAM_B]
