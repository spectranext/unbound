from typing import List, Dict
from .. import Contract
from . placebase import PlaceBase
from .. delivery import ItemDeliveryContract
from ... import items
from ... import loc

# Progression: ~6 ingot_iron equivalent (delivery-3) -> ~16 ingot_iron (delivery-4) via sheets then bolts.
CONTRACTS: List[Contract] = [
    PlaceBase(
        "fresh-air", "oxygen_tank", 300,
        loc.TASK_OXYGEN, loc.TASK_OXYGEN_ACTION, loc.TASK_OXYGEN_ACTION_DESC),
    ItemDeliveryContract(
        "delivery-1", loc.CONTRACT_PROBATION, 600,
        {items.get("ground"): 10}),
    ItemDeliveryContract(
        "delivery-2", loc.CONTRACT_IRON_INGOTS, 1000,
        {items.get("ingot_iron"): 4}),
    ItemDeliveryContract(
        "delivery-3", loc.CONTRACT_SHEETS, 1500,
        {items.get("sheet_iron"): 12}),
    ItemDeliveryContract(
        "delivery-4", loc.CONTRACT_BOLTS, 2400,
        {items.get("bolt_iron"): 256}),
]

CONTRACTS_BY_ID: Dict[str, Contract] = {
    c.contract_id: c
    for c in CONTRACTS
}
