from typing import Optional, List, Union, Dict, Tuple, TYPE_CHECKING

if TYPE_CHECKING:
    from . player import PlayerObject
    from . inventory import Inventory
    from . team import Team

from . api.query import QueryResponse, QueryResponseOption, OPT, NOACT
from . api.client import ClientAPI
from . import items
from . import loc, icons


class CraftingRecipySeeMoreQueryResponse(QueryResponse):

    def __init__(self, recipy: 'CraftingRecipy', player: 'PlayerObject'):
        from . player.wiki import WikiEntryQueryResponse, WikiDataEntry

        super().__init__(b"", recipy.item.name.encode())
        self.player = player
        self.recipy = recipy
        self.description = loc.RECIPY_INGREDIENTS.format(
            recipy.item.description,
            self.player.get_inventory().count_items(recipy.item)).encode()
        self.options = []

        if self.recipy.enough_bool(player.get_inventory()):
            self.options.extend([
                OPT(loc.CRAFT_CRAFT_2.format(recipy.item.name), NOACT(self.craft),
                    icon=icons.ICON_CRAFTING)
            ])

        self.options.extend([
            OPT(loc.BACK.format(recipy.item.name), NOACT(CraftingQueryResponse, b"", self.player),
                icon=icons.ICON_EXIT)
        ])

        for ing, am in recipy.consumables.items():
            t = loc.CRAFT_INGREDIENT_STATUS.format(am, ing.name)

            self.options.append(
                OPT(t, NOACT(WikiEntryQueryResponse,  player, WikiDataEntry(ing)),
                    icon=ing.icon))

            if self.recipy.enough_item(player.get_inventory(), ing):
                self.options.append(
                    OPT("  " + loc.CRAFT_ENOUGH_ITEM.format(player.get_inventory().count_items(ing)),
                        icon=icons.ICON_HAVE))
            else:
                rc = CraftingRecipes.find(ing)

                self.options.append(
                    OPT("  " + loc.CRAFT_NOT_ENOUGH_ITEM.format(player.get_inventory().count_items(ing)),
                        icon=icons.ICON_CLEAR_CART))

                if rc:
                    self.options.append(
                        OPT("  " + loc.CRAFT_CRAFT_THIS_ITEM.format(player.get_inventory().count_items(ing)),
                            NOACT(CraftingRecipySeeMoreQueryResponse, rc, player),
                            icon=icons.ICON_CRAFTING))

        self.actions = [loc.CRAFT_ITEM_WIKI.encode(), loc.OK.encode()]

    def craft(self) -> Optional['QueryResponse']:
        itm = self.recipy.enough(self.player.get_inventory())
        if isinstance(itm, items.Item):
            self.player.client.queue_notify(
                loc.CRAFT_NOT_ENOUGH_ITEM.format(itm.name).encode(),
                ClientAPI.NOTIFY_MESSAGE_COLOR_DANGER)
            return None
        crafted_item, crafted_amount = self.recipy.craft(self.player)
        self.player.add_to_inventory(crafted_item, crafted_amount, 1.)
        self.player.client.queue_notify(
            loc.CRAFT_CRAFTED.format(crafted_item.name).encode(),
            ClientAPI.NOTIFY_MESSAGE_COLOR_BRIGHT, context="crafting")
        return CraftingRecipySeeMoreQueryResponse(self.recipy, self.player)


class CraftingRecipy(QueryResponseOption):
    def __init__(self, item: items.Item, consumables: Dict[items.Item, int], amount: int = 1):
        self.item = item
        self.consumables = consumables
        self.amount = amount

    def enough(self, inventory: 'Inventory') -> Union[bool, items.Item]:
        for ing, am in self.consumables.items():
            if not inventory.has_item(ing):
                return ing
            if inventory.count_items(ing) < am:
                return ing
        return True

    def enough_bool(self, inventory: 'Inventory') -> bool:
        for ing, am in self.consumables.items():
            if not inventory.has_item(ing):
                return False
            if inventory.count_items(ing) < am:
                return False
        return True

    def enough_item(self, inventory: 'Inventory', item: items.Item) -> bool:
        if not inventory.has_item(item):
            return False
        if inventory.count_items(item) < self.consumables.get(item):
            return False
        return True

    def matches_consumables(self, item: items.Item):
        return item in self.consumables.keys()

    def craft(self, player: 'PlayerObject') -> Tuple[items.Item, int]:
        for ing, am in self.consumables.items():
            player.remove_from_inventory(ing, am)
        return self.item, self.amount

    def see_more(self, player: 'PlayerObject') -> QueryResponse:
        return CraftingRecipySeeMoreQueryResponse(self, player)

    def icon(self):
        return self.item.icon

    def __str__(self):
        return self.item.name


class CraftingRecipes(object):
    ALL: List[CraftingRecipy] = []

    @staticmethod
    def parse(it, itm: items.Item):
        CraftingRecipes.ALL.append(
            CraftingRecipy(itm, {
                items.get(k1): v1
                for k1, v1 in it["required"].items()
            }, int(it.get("amount", 1))))

    @staticmethod
    def find(itm: items.Item) -> Optional[CraftingRecipy]:
        for r in CraftingRecipes.ALL:
            if r.item == itm:
                return r
        return None

    @staticmethod
    def suggest(team: 'Team'):
        if not team:
            return []
        return [
            r
            for r in CraftingRecipes.ALL
        ]


class CraftingQueryResponse(QueryResponse):
    def __init__(self, query: bytes, player: 'PlayerObject'):
        self.player = player
        super().__init__(query, loc.CRAFT_CRAFTING.encode())
        self.options = CraftingRecipes.suggest(player.get_team())
        self.actions = [loc.SEE_MORE.encode()]

    def selected(self, option: int, action: bytes):
        if action == loc.SEE_MORE.encode():
            if option >= len(self.options):
                return None
            return self.options[option].see_more(self.player)
        return None
