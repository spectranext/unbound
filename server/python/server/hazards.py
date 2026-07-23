import random

from . import items
from . api.block import BlockObject, NeighboringBlockObject, NeighboringSet
from . api.map import MapAPI
from . api.object import ObjectAPI
from . effects import MapEffects
from . linkedblocks import LinkedBlockObject


class SpikeBlockObject(NeighboringBlockObject):
    def __init__(self, neighboring_set: NeighboringSet, touch_damage: int = 5, overlap_damage: int = 25, **kwargs):
        super().__init__(neighboring_set, is_blocking=False, **kwargs)
        self.touch_damage = touch_damage
        self.overlap_damage = overlap_damage

    def on_touch(self, player: ObjectAPI) -> bool:
        if not player.is_player():
            return False

        if player.get_y() >= self.y + 1:
            return False

        player.damage(self.touch_damage, "spikes")
        return False

    def on_overlap(self, player: ObjectAPI) -> bool:
        if not player.is_player():
            return False

        player.damage(self.overlap_damage, "spikes")
        return True


class PopstoneBlockObject(LinkedBlockObject):
    def __init__(self, code: int = 0, **kwargs):
        super().__init__(code, **kwargs)
        self.armed = self.code >= 244

    def _link_blocks(self):
        if self.link_id is None:
            return [self]

        link_blocks = MapAPI.instance.obtain_link(self.link_id)
        if not link_blocks:
            return [self]

        return link_blocks

    def _arm_link(self, link_blocks):
        for block in link_blocks:
            if isinstance(block, PopstoneBlockObject):
                block.armed = True
            if block.code < 244:
                block.code += 4
            MapAPI.instance.update_block(block.x, block.y, True)

    def _clear_link(self, link_blocks):
        if self.link_id is not None:
            link_list = MapAPI.instance.obtain_link(self.link_id)
            if link_list is not None:
                link_list.clear()
            MapAPI.instance.deallocate_link(self.link_id)

        for block in link_blocks:
            new_block = BlockObject(0, items.NOTHING)
            new_block.copy_light(block)
            MapAPI.instance.set_block(block.x, block.y, new_block, True)
            new_block.refresh()

        MapAPI.instance.schedule_map_refresh(False)

    def _effect_center(self, link_blocks):
        blocks = link_blocks or [self]
        x = sum(block.x for block in blocks) / len(blocks) + 0.5
        y = sum(block.y for block in blocks) / len(blocks) + 0.5
        return x, y

    def _still_on_map(self, link_blocks):
        for block in link_blocks:
            current = MapAPI.instance.get_block(block.x, block.y)
            if current is block:
                return True
        return False

    def _damage_nearby_players(self, x: float, y: float):
        for obj in MapAPI.instance.query_objects(int(x - 4), int(y - 4), 8, 8):
            distance = ((obj.get_x() - x) ** 2 + (obj.get_y() - y) ** 2) ** 0.5
            if distance <= 4:
                obj.damage(25, "popstone")

    def _send_explosion_effect(self, x: float, y: float):
        MapAPI.instance.send_effect(
            x + random.randint(-8, 8) / 8.0,
            y + random.randint(-8, 8) / 8.0,
            MapEffects.EXPLOSION)

    def _explode(self, link_blocks):
        if not self._still_on_map(link_blocks):
            return

        x, y = self._effect_center(link_blocks)
        self._damage_nearby_players(x, y)
        self._clear_link(link_blocks)
        self._send_explosion_effect(x, y)

        for index in range(1, 3):
            def explode_effect():
                self._send_explosion_effect(x, y)

            MapAPI.instance.schedule_callback(explode_effect, index * 200)

    def _schedule_explosion(self, link_blocks):
        blocks = link_blocks.copy()

        def explode():
            self._explode(blocks)

        MapAPI.instance.schedule_callback(explode, 2000)

    def on_touch(self, player: ObjectAPI) -> bool:
        if self.armed:
            return False

        self._explode(self._link_blocks())
        return True

    def on_overlap(self, player: ObjectAPI) -> bool:
        if self.armed:
            return True

        link_blocks = self._link_blocks()
        self._arm_link(link_blocks)
        self._schedule_explosion(link_blocks)
        return True
