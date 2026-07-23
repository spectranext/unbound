from typing import Dict, Tuple, List, TYPE_CHECKING

if TYPE_CHECKING:
    from . import Item

class GenerationLayer(object):
    def __init__(self):
        self.weights: Dict['Item', float] = {}
        self.layer_limits: Dict['Item', Tuple[int, int]] = {}
        self.skip = 0
        self.smoothness = 1
        self.high_limit = None
        self.low_limit = None

    def parse(self, it):
        from . import get_item

        self.skip = it["skip"]
        self.smoothness = it["smoothness"]
        if "high_limit" in it:
            self.high_limit = it["high_limit"]
        if "low_limit" in it:
            self.low_limit = it["low_limit"]
        weights = it["weights"]

        for k, v in weights.items():
            itm = get_item(k)
            self.weights[itm] = v['probability']
            if 'layer_limits' in v:
                l = v['layer_limits']
                self.layer_limits[itm] = (l["from"], l["to"])

class Generation(object):
    BASE_BLOCK: str = None
    LAYERS: List[GenerationLayer] = []
    NOTHING: float = 0
    MIDDLE_LINE: float = 38

    @staticmethod
    def parse(it):
        Generation.BASE_BLOCK = it["base"]
        Generation.MIDDLE_LINE = it["middle_line"]
        for data in it["layers"]:
            layer = GenerationLayer()
            layer.parse(data)
            Generation.LAYERS.append(layer)
