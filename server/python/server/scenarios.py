from typing import Dict, Optional


class Scenario(object):
    def __init__(self, spawn_immediately: bool, map_wave_a: float, map_wave_b: float, trees: bool, light: bool,
                 auto_login: bool, respawn: bool, seconds_in_a_day: int, bots: bool, reward_screen: bool,
                 tutorial: bool, map_width_chunks: Optional[int] = None, bank_fees: bool = True,
                 bank_start_bonus: int = 0):
        self.spawn_immediately = spawn_immediately
        self.map_wave_a = map_wave_a
        self.map_wave_b = map_wave_b
        self.trees = trees
        self.light = light
        self.auto_login = auto_login
        self.respawn = respawn
        self.seconds_in_a_day = seconds_in_a_day
        self.bots = bots
        self.reward_screen = reward_screen
        self.tutorial = tutorial
        self.map_width_chunks = map_width_chunks
        self.bank_fees = bank_fees
        self.bank_start_bonus = bank_start_bonus

wave_a = 6
wave_b = 4


SCENARIOS: Dict[str, Scenario] = {
    "default": Scenario(
        False, wave_a, wave_b, True,
        True, False, False, 480, True, True, True),
    "default_no_login": Scenario(
        False, wave_a, wave_b, True,
        True, True, False, 480, True, True, True),
    "spawn_no_login": Scenario(
        True, wave_a, wave_b, True, True,
        True, False, 480, True, True, False),
    "no_login": Scenario(
        False, wave_a, wave_b, True, True,
        True, False, 480, True, True, True),
    "debug": Scenario(
        True, wave_a, wave_b, True, True,
        True, False, 480, True, False, False),
    "no_light": Scenario(
        True, wave_a, wave_b, True, False,
        True, False, 480, True
        , False, False),
    "flat": Scenario(
        True, 0, 0, True, True,
        True, False, 480, False, False, False),
    "respawn": Scenario(
        False, 0, 0, False, False,
        False, True, 480, True, False, False),
    "demo": Scenario(
        True, wave_a, wave_b, True, True,
        True, True, 480, True, False, False, bank_fees=False, bank_start_bonus=10000),
}


def get_scenario(nm: bytes) -> Scenario:
    s = nm.decode()
    return SCENARIOS.get(s, SCENARIOS["default"])
