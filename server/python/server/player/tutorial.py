
from typing import List, Tuple
from .. art import ICE, CONTRACT_WAX, ROBOT, ENVIRONMENT, PRICE_ACTION, HR, OXYGEN, KEYBINDS, AIM, MOVE
from .. art import DIGGING
from .. import loc


TUTORIAL_DIALOGS: List[Tuple[bytes, bytes, bytes]] = [
    (HR, loc.TUTORIAL_SCREEN_1_TITLE.encode(), loc.TUTORIAL_SCREEN_1_DESC.encode()),
    (MOVE, loc.TUTORIAL_MOVE.encode(), None),
    (AIM, loc.TUTORIAL_AIM.encode(), None),
    (OXYGEN, loc.TUTORIAL_SCREEN_2_TITLE.encode(), loc.TUTORIAL_SCREEN_2_DESC.encode()),
    (ICE, loc.TUTORIAL_SCREEN_3_TITLE.encode(), loc.TUTORIAL_SCREEN_3_DESC.encode()),
    (CONTRACT_WAX, loc.TUTORIAL_SCREEN_4_TITLE.encode(), loc.TUTORIAL_SCREEN_4_DESC.encode()),
    (PRICE_ACTION, loc.TUTORIAL_SCREEN_5_TITLE.encode(), loc.TUTORIAL_SCREEN_5_DESC.encode()),
    (ENVIRONMENT, loc.TUTORIAL_SCREEN_6_TITLE.encode(), loc.TUTORIAL_SCREEN_6_DESC.encode()),
    (ROBOT, loc.TUTORIAL_SCREEN_7_TITLE.encode(), loc.TUTORIAL_SCREEN_7_DESC.encode()),
    (KEYBINDS, loc.TUTORIAL_KEY_HINTS.encode(), loc.TUTORIAL_KEY_HINTS_DESC.encode()),
    (DIGGING, loc.DIGGING_HINTS.encode(), loc.DIGGING_HINT_DESC.encode()),
]