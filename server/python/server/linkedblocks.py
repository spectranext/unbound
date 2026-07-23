from typing import Optional, Dict
from . api.map import MapAPI
from . api.block import BlockObject, NeighboringBlockObject
from . import items
from . import blocks


class LinkedBlockObject(BlockObject):
    def __init__(self, code: int = 0, link: Optional[int] = 0, trigger: bool = True,
                 tag: Optional[str] = None, proto: Dict[bytes, bytes] = None, **kwargs):
        super(LinkedBlockObject, self).__init__(code, tag=tag, proto=proto, **kwargs)
        self.trigger = trigger
        if link:
            self.link_id = link
            link_list = MapAPI.instance.obtain_link(link)
            if link_list is not None:
                link_list.append(self)
        elif proto and b"l" in proto:
            self.link_id = int.from_bytes(proto[b"l"], "little")
            link_list = MapAPI.instance.obtain_link(self.link_id)
            if link_list is not None:
                link_list.append(self)
        else:
            self.link_id = None

    def get_good_light_code(self, light: int):
        if self.blocking():
            return blocks.GROUND_15_SHADE_1
        return 0

    def get_midrange_light_code(self, light: int):
        if self.blocking():
            return blocks.GROUND_15_SHADE_2
        return 0

    def get_some_light_code(self, light: int):
        if self.blocking():
            return blocks.GROUND_15_SHADE_3
        return 0

    def serialize_attrs(self) -> Dict[bytes, bytes]:
        d = super().serialize_attrs()
        d[b"l"] = self.link_id.to_bytes(4, "little")
        return d

    def release(self, notify: bool):
        from . player import PlayerObject

        if self.link_id is None:
            return

        link_list = MapAPI.instance.obtain_link(self.link_id)

        if not link_list:
            # someone already cleared the link
            # or there is no link
            return

        if not self.trigger:
            return

        mv_link = link_list.copy()
        link_list.clear()
        MapAPI.instance.deallocate_link(self.link_id)

        if self.toucher is None:
            return

        player_toucher: PlayerObject = self.toucher

        any_removed = False
        for block in mv_link:
            if block == self:
                # do not remove ourselves because we're already being removed
                continue
            r = block.item.remove_immediately(player_toucher)
            new_b = BlockObject(0, items.NOTHING)
            MapAPI.instance.set_block(block.x, block.y, new_b, True)
            new_b.copy_light(block)
            new_b.refresh()
            if isinstance(new_b, NeighboringBlockObject):
                new_b.refresh_neighbors(True)
            any_removed = True
            if r == items.NOTHING:
                continue
            player_toucher.add_to_inventory(r, 1, 1.)

        if any_removed:
            MapAPI.instance.schedule_map_refresh(False)
