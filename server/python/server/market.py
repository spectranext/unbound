import time
from collections import deque
from typing import Deque, Dict, Tuple, TYPE_CHECKING

if TYPE_CHECKING:
    from .items import Item

# Reference buy price for value=0 and value=100 (before supply/demand).
CHEAP_ITEM = 10
EXPENSIVE_ITEM = 500

# Initial sell (buyback) price is this fraction of base buy at equal conditions.
SELL_FRACTION = 0.5

# Rolling window for transaction pressure (seconds).
HISTORY_TTL_SEC = 86400

# Per-unit demand/supply effect over HISTORY_TTL_SEC (pressure is summed qty).
# 0.003 made mid-tier items (e.g. ~103cr) stay at the same integer after one purchase.
BUY_SENSITIVITY = 0.012
SELL_SENSITIVITY = 0.012
_PRESSURE_CAP = 80.0


class Market(object):
    """Dynamic Star Store pricing from relative item values and recent buy/sell volume."""

    _instance: 'Market' = None

    def __init__(self):
        self._base_buy: Dict['Item', float] = {}
        self._base_sell: Dict['Item', float] = {}
        self._buy_history: Dict['Item', Deque[Tuple[float, int]]] = {}
        self._sell_history: Dict['Item', Deque[Tuple[float, int]]] = {}

    @classmethod
    def instance(cls) -> 'Market':
        if cls._instance is None:
            cls._instance = Market()
        return cls._instance

    @classmethod
    def bootstrap(cls) -> None:
        from .tuning import Tuning

        m = cls.instance()
        m._base_buy.clear()
        m._base_sell.clear()
        m._buy_history.clear()
        m._sell_history.clear()

        for item, si in Tuning.PRICING.items():
            v = max(0.0, min(100.0, float(si.value)))
            t = v / 100.0
            bb = CHEAP_ITEM + (EXPENSIVE_ITEM - CHEAP_ITEM) * t
            m._base_buy[item] = bb
            m._base_sell[item] = bb * SELL_FRACTION
            m._buy_history[item] = deque()
            m._sell_history[item] = deque()

    def _prune(self, history: Deque[Tuple[float, int]], now: float) -> None:
        while history and now - history[0][0] > HISTORY_TTL_SEC:
            history.popleft()

    def _buy_pressure(self, item: 'Item', now: float) -> float:
        h = self._buy_history.get(item)
        if not h:
            return 0.0
        self._prune(h, now)
        return float(sum(q for _, q in h))

    def _sell_pressure(self, item: 'Item', now: float) -> float:
        h = self._sell_history.get(item)
        if not h:
            return 0.0
        self._prune(h, now)
        return float(sum(q for _, q in h))

    def get_buy_price(self, item: 'Item') -> int:
        from .tuning import Tuning

        si = Tuning.PRICING.get(item)
        if not si or not si.purchasable():
            return 0
        base = self._base_buy.get(item, 0.0)
        now = time.time()
        p = min(_PRESSURE_CAP, self._buy_pressure(item, now))
        mult = 1.0 + BUY_SENSITIVITY * p
        return max(1, int(round(base * mult)))

    def get_sell_price(self, item: 'Item') -> int:
        from .tuning import Tuning

        si = Tuning.PRICING.get(item)
        if not si or not si.allow_buyback:
            return 0
        base = self._base_sell.get(item, 0.0)
        now = time.time()
        p = min(_PRESSURE_CAP, self._sell_pressure(item, now))
        mult = max(0.1, 1.0 - SELL_SENSITIVITY * p)
        return max(0, int(round(base * mult)))

    def record_buy(self, item: 'Item', qty: int) -> None:
        if qty <= 0 or item not in self._buy_history:
            return
        self._buy_history[item].append((time.time(), qty))

    def record_sell(self, item: 'Item', qty: int) -> None:
        if qty <= 0 or item not in self._sell_history:
            return
        self._sell_history[item].append((time.time(), qty))
