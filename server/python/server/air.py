from typing import Set, List, Tuple

class AirProducer(object):
    def __init__(self):
        self._pressure_zone_blocks: List[Tuple[int, int]] = []

    def set_pressure_zone_blocks(self, pressure_zone_blocks: List[Tuple[int, int]]):
        self._pressure_zone_blocks = pressure_zone_blocks

    def get_pressure_zone_blocks(self):
        return self._pressure_zone_blocks

    def has_pressure(self):
        return len(self._pressure_zone_blocks) > 0

    def get_x(self) -> int:
        pass

    def get_y(self) -> int:
        pass

class Air(object):
    def __init__(self):
        self.objects: Set[AirProducer] = set()

    def register(self, o: AirProducer):
        self.objects.add(o)

    def unregister(self, o: AirProducer):
        self.objects.remove(o)

    def get_all_producers(self) -> Set[AirProducer]:
        return self.objects
