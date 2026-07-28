from typing import Optional, Union, TYPE_CHECKING

from . api.object import ObjectAPI
from . api.block import BlockObject, NeighboringBlockObject
from . api.touch import PostponedTouch
from . api.map import MapAPI
from . api.client import ClientAPI
from . linkedblocks import LinkedBlockObject
from . inventory import InventoryRecord
from . items import Item, ItemRemoveState, NOTHING, PlacingError
from . blockspawn import BlockSpawn
from . bases.worm import WormBaseItem
from . import loc

if TYPE_CHECKING:
    from . player import PlayerObject

import time


class DelayedObjectRemoval(PostponedTouch):
    def __init__(self, x: int, y: int, block_to_obtain: Item, num_items: int, deadline: int,
                 relevant_record: Optional[InventoryRecord],
                 player: 'PlayerObject', old_block: BlockObject):
        super(DelayedObjectRemoval, self).__init__(deadline)
        self.x = x
        self.y = y
        self.block_to_obtain = block_to_obtain
        self.relevant_record = relevant_record
        self.num_items = num_items
        self.player = player
        self.started_at = time.time()
        self.deadline = self.started_at + float(deadline) / 1000.
        self.old_block = old_block
        if old_block.item.remove_state == ItemRemoveState.PICKING:
            player.picking_state = True
            player.reset_object_state()

    def update(self) -> Union[int, bool]:
        now = time.time()
        if now > self.deadline:
            if self.relevant_record:
                if self.relevant_record.item.degradation_per_hit:
                    self.relevant_record.health -= self.num_items * self.relevant_record.item.degradation_per_hit
                    if self.relevant_record.health <= 0:
                        def notify():
                            client = self.player.get_client()
                            if not client:
                                return
                            client.queue_notify(
                                loc.INVENTORY_WEAPON_DESTROYED.format(self.relevant_record.item.name).encode(),
                                ClientAPI.NOTIFY_MESSAGE_COLOR_WARNING)
                        MapAPI.instance.schedule_callback(notify, 2000)
                        self.player.remove_record_from_inventory(self.relevant_record, 1)

            self.old_block.toucher = self.player
            if self.block_to_obtain.credits:
                self.player.get_team().add_credits(self.block_to_obtain.credits)
                client = self.player.get_client()
                if client:
                    client.queue_notify(
                        "+{0} credits".format(self.block_to_obtain.credits).encode(),
                        ClientAPI.NOTIFY_MESSAGE_COLOR_REGULAR, "new_credits")
            else:
                self.player.add_to_inventory(self.block_to_obtain, 1, 1.)
            new_b = BlockObject(0, NOTHING)
            MapAPI.instance.set_block(self.x, self.y, new_b, True)
            new_b.copy_light(self.old_block)
            new_b.refresh()

            if isinstance(new_b, NeighboringBlockObject):
                new_b.refresh_neighbors(True)

            # Track block removal for worm digging tracker
            WormBaseItem.track_block_removed(self.player, self.x, self.y, self.block_to_obtain)

            MapAPI.instance.schedule_map_refresh(False)
            return True
        progress = int(12. * ((now - self.started_at) / (self.deadline - self.started_at)))
        return progress

    def dispose(self):
        if self.player.picking_state:
            self.player.picking_state = False
            self.player.reset_object_state()


def touch(p: ObjectAPI, x: int, y: int) -> Union[None, PostponedTouch]:
    MapAPI.instance.print("Player {0} touched {1}x{2}".format(p.get_client_id(), x, y))

    client = p.get_client()
    selected_record = client.get_selected_record() if client else None
    if selected_record:
        item = selected_record.item
        if item.on_touch(p, x, y):
            return None

    if y < MapAPI.instance.top_placing_block:
        client = p.get_client()
        if client:
            client.queue_notify(
                loc.CANNOT_PLACE_ABOVE.format(MapAPI.instance.top_placing_block).encode(),
                ClientAPI.NOTIFY_MESSAGE_COLOR_DANGER)
        return

    b = MapAPI.instance.get_block(x, y)
    if b.code:
        block_touch = b.on_touch(p)
        if isinstance(block_touch, PostponedTouch):
            return block_touch
        if block_touch:
            return None

        if not b.item:
            return None

        try:
            remove_block, time_to_remove, relevant_record = b.item.remove(p)
        except PlacingError as e:
            client = p.get_client()
            if client:
                client.queue_notify(e.message.encode(), ClientAPI.NOTIFY_MESSAGE_COLOR_DANGER)
            return None
        num_items = 1
        if isinstance(b, LinkedBlockObject) and b.link_id:
            ls = MapAPI.instance.obtain_link(b.link_id)
            num_items = len(ls)
            if ls:
                time_to_remove *= len(ls)
        if time_to_remove > 0:
            return DelayedObjectRemoval(
                x, y, remove_block, num_items, time_to_remove, relevant_record, p, b)
        if relevant_record:
            if relevant_record.item.degradation_per_hit:
                relevant_record.health -= num_items * relevant_record.item.degradation_per_hit
                if relevant_record.health <= 0:
                    def notify():
                        c2 = p.get_client()
                        if c2:
                            c2.queue_notify(
                                loc.INVENTORY_WEAPON_DESTROYED.format(relevant_record.item.name).encode(),
                                ClientAPI.NOTIFY_MESSAGE_COLOR_WARNING)
                    MapAPI.instance.schedule_callback(notify, 2000)
                    p.remove_record_from_inventory(relevant_record, 1)
        b.toucher = p
        if remove_block.credits:
            p.get_team().add_credits(remove_block.credits)
            client = p.get_client()
            if client:
                client.queue_notify(
                    "+{0} credits".format(remove_block.credits).encode(),
                    ClientAPI.NOTIFY_MESSAGE_COLOR_REGULAR, "new_credits")
        else:
            p.add_to_inventory(remove_block, 1, 1.)
        new_b = BlockObject(0, NOTHING)
        
        # Track block removal for worm digging tracker
        WormBaseItem.track_block_removed(p, x, y, remove_block)
    else:
        if selected_record is None:
            return None
        item = selected_record.item
        try:
            placed_item = item.place(p, x, y)
        except PlacingError as e:
            if client:
                client.queue_notify(e.message.encode(), ClientAPI.NOTIFY_MESSAGE_COLOR_DANGER)
            return None
        if not p.remove_from_inventory(item):
            return None
        if placed_item is None:
            return None
        else:
            new_b = BlockSpawn.create_block(placed_item)
            if new_b is None:
                return None
        if client and client.get_selected_record() is None:
            client.queue_notify(loc.YOU_HAVE_PLACED_LAST.format(item.name).encode(),
                                ClientAPI.NOTIFY_MESSAGE_COLOR_WARNING)

    new_b.copy_light(b)
    MapAPI.instance.set_block(x, y, new_b, True)
    new_b.refresh()

    if isinstance(new_b, NeighboringBlockObject):
        new_b.refresh_neighbors(True)

    MapAPI.instance.schedule_map_refresh(False)
    return None
