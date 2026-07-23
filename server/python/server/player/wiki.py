from typing import Dict, TYPE_CHECKING, List

from .. api.query import QueryResponse, OPT, NOACT
from .. import loc
from .. items import Item
from .. bases import BaseItem
from .. market import Market

if TYPE_CHECKING:
    from .. import PlayerObject


def _market_wiki_lines(item: Item) -> List[str]:
    sp = item.get_store_pricing()
    if not sp:
        return []
    m = Market.instance()
    lines: List[str] = [
        "",
        loc.WIKI_MARKET_HEADER,
    ]
    if sp.purchasable():
        buy = m.get_buy_price(item)
        lines.append(loc.WIKI_MARKET_BUY.format(buy, sp.amount))
    if sp.allow_buyback:
        sell = m.get_sell_price(item)
        lines.append(loc.WIKI_MARKET_SELL.format(sell))
    if len(lines) <= 2:
        return []
    lines.append(loc.WIKI_MARKET_BLURB)
    return lines


class WikiDataEntry(object):
    def __init__(self, i: Item):
        self.item = i
        self.icon = i.icon
        self.title = i.name
        self.desc = i.description if i.description else i.name


class WikiData(object):
    def __init__(self):
        self.entries: Dict[str, WikiDataEntry] = {}
        for k, v in Item.ITEMS.items():
            if not v.simple_item() and not isinstance(v, BaseItem):
                continue
            self.entries[k] = WikiDataEntry(v)


wiki_data = WikiData()


class WikiEntryCraftingQueryResponse(QueryResponse):
    def __init__(self, p: 'PlayerObject', e: WikiDataEntry):
        super().__init__(b"", e.title.encode())
        self.description = "Crafting:".encode()
        self.options = []

        for r in e.item.get_crafting_recipies():
            for ing, am in r.consumables.items():
                t = "{0} of {1}".format(am, ing.name)
                if not r.enough_item(p.get_team().inventory, ing):
                    t = "[NO] " + t

                self.options.append(
                    OPT(t, NOACT(WikiEntryQueryResponse,  p, WikiDataEntry(ing)), icon=ing.icon))
            break

        self.actions = [loc.CRAFT_ITEM_WIKI.encode()]


class WikiEntryQueryResponse(QueryResponse):
    def __init__(self, p: 'PlayerObject', e: WikiDataEntry):
        super().__init__(b"", e.title.encode())
        desc = e.desc
        extra = _market_wiki_lines(e.item)
        if extra:
            desc = desc + "\n" + "\n".join(extra)
        self.description = desc.encode()
        if e.item.get_crafting_recipies():
            self.options = [
                OPT("Crafting", NOACT(WikiEntryCraftingQueryResponse,  p, e))
            ]
        self.actions = [loc.OK.encode()]


class WikiQueryResponse(QueryResponse):
    def __init__(self, p: 'PlayerObject'):
        super().__init__(b"", loc.WIKI.encode())
        self.options = [
            OPT(v.title, NOACT(WikiEntryQueryResponse,  p, v), icon=v.icon)
            for k, v in wiki_data.entries.items()
        ]
        self.actions = [loc.OK.encode()]

