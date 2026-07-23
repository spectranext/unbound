from dataclasses import dataclass
from typing import Dict, Union, List, Tuple, Optional, TYPE_CHECKING
import time
import math
import random

from . client import Client, WeaponState
from .. api.client import ClientAPI, QueuedNotification
from .. api.object import ObjectAPI
from .. api.touch import PostponedTouch
from .. api.map import MapAPI
from .. api.query import QueryResponse
from .. inventory import Inventory, InventoryRecord
from .. import hit
from .. import items
from .. import loc

if TYPE_CHECKING:
    from .. team import Team


@dataclass
class AddToInventoryCtx:
    amount: int
    total_amount: int
    item_name: str


class PlayerObject(ObjectAPI):
    HOLDING_NOTHING = "!nothing"
    PLAYER = b"player"
    SAFE_FALL_SPEED = 4
    FALL_DAMAGE_PER_SPEED = 10

    PLAYER_STATE_GUN_0 = 2
    PLAYER_STATE_GUN_45 = 3
    PLAYER_STATE_GUN_90 = 4
    PLAYER_STATE_GUN_135 = 5
    PLAYER_STATE_GUN_180 = 6

    def __init__(self, player_id: int, c: Client):
        super(PlayerObject, self).__init__(ObjectAPI.OBJECT_TYPE_PLAYER, ObjectAPI.DATA_ENTRY_CHAR1,
                                           ObjectAPI.DATA_ENTRY_CHAR1_MOVING,
                                           ObjectAPI.DATA_ENTRY_CHAR1_PICKING)
        self.client = c
        self.player_id = player_id
        self.tutorial = False
        self.state: bytes = PlayerObject.HOLDING_NOTHING.encode()
        self.power_test = time.time() + self.power_consumption
        self.oxygen_test = time.time() + 0.5
        self.heat_test = time.time() + 1
        self.temperature_test = time.time() + 3
        self.hit_cooldown = None
        self.tmp_movement: Tuple[int, int] = (0, 0)
        self.angle = 90
        self.credits = 0
        self.picking_state = False
        self.pressure = False
        self.god = False

    def identity(self) -> bytes:
        return PlayerObject.PLAYER

    def is_player(self) -> bool:
        return True

    def get_state_flags(self) -> int:
        flags = self.client.get_state_flags()
        if self.client.equipped_weapon:
            flags |= ObjectAPI.STATE_FLAG_AIM
            if self.client.is_reloading():
                flags |= ObjectAPI.STATE_FLAG_FLASHING
        return flags

    def test_pressure(self) -> bool:
        b = MapAPI.instance.get_block(int(self.get_x()), int(self.get_y()))
        if b is None:
            return False
        return b.get_air() > 0

    def update_pressure(self):
        pressure = self.test_pressure()

        if self.pressure == pressure:
            return

        self.pressure = pressure

        if self.pressure:
            # just gained pressure
            self.client.queue_notify(loc.AIR_PRESSURE.encode(), ClientAPI.NOTIFY_MESSAGE_COLOR_SUCCESS)
        else:
            # lost pressure
            self.client.queue_notify(loc.AIR_PRESSURE_LOST.encode(), ClientAPI.NOTIFY_MESSAGE_COLOR_DANGER)

    def on_update(self, server_map: 'MapAPI'):
        super().on_update(server_map)
        tm = time.time()
        do_sync_stats = False
        t = self.get_team()
        if t:
            self.credits = t.credits
        if self.heat_time > 0:
            if tm > self.heat_test:
                self.heat_test = tm + 1
                self.heat_time -= 1
                if self.temperature < self.get_max_temperature():
                    self.temperature = min(self.get_max_temperature(), self.temperature + 3)
                    do_sync_stats = True

        if tm > self.oxygen_test:
            self.oxygen_test = tm + 0.5

            b = MapAPI.instance.get_block(int(self.get_x()), int(self.get_y()))
            if b and b.has_air() and (self.power < self.get_max_power()):
                if b.pull_air():
                    self.power += 1
                    do_sync_stats = True
                    if self.power >= self.get_max_power():
                        self.client.queue_notify(loc.OXYGEN_TANK_REFILLED.format("100").encode(), ClientAPI.NOTIFY_MESSAGE_COLOR_SUCCESS)

            self.update_pressure()
        elif tm > self.power_test:
            self.power_test = tm + self.power_consumption
            if self.power > 0:
                b = MapAPI.instance.get_block(int(self.get_x()), int(self.get_y()))
                if b and b.has_air():
                    b.pull_air()
                else:
                    self.power -= 1
                    if self.power == 20:
                        self.client.queue_notify(loc.OXYGEN_LOW_LEVEL_DANGER.format(self.power).encode(), ClientAPI.NOTIFY_MESSAGE_COLOR_DANGER)
                    elif self.power <= 5:
                        self.client.queue_notify(loc.OXYGEN_LOW_LEVEL_CRITICAL.format(self.power).encode(), ClientAPI.NOTIFY_MESSAGE_COLOR_DANGER)
                    elif self.power % 20 == 0:
                        self.client.queue_notify(loc.OXYGEN_LEVEL.format(self.power).encode(), ClientAPI.NOTIFY_MESSAGE_COLOR_REGULAR)
                    do_sync_stats = True
            else:
                self.damage(25)
                do_sync_stats = True
        elif tm > self.temperature_test:
            self.temperature_test = tm + 3

            if self.temperature <= self.get_min_temperature():
                self.damage(5, loc.CRITICAL_TEMPERATURE.format(self.temperature).encode())
                do_sync_stats = True

            if MapAPI.instance.is_cold():
                b = MapAPI.instance.get_block(int(self.get_x()), int(self.get_y()))
                bump_temp = b and b.has_air()
            else:
                bump_temp = True

            if bump_temp:
                if self.temperature < self.get_max_temperature():
                    self.temperature = min(self.get_max_temperature(), self.temperature + 5)
                do_sync_stats = True
            else:
                if self.temperature > self.get_min_temperature():

                    prev_temp = self.temperature

                    def dropped(value: int) -> bool:
                        return (self.temperature < value) and (prev_temp >= value)

                    self.temperature -= 2

                    if dropped(25):
                        self.client.queue_notify(loc.HEAT_LOW_LEVEL_DANGER.format(self.temperature).encode(), ClientAPI.NOTIFY_MESSAGE_COLOR_DANGER)
                    elif dropped(8):
                        self.client.queue_notify(loc.HEAT_LOW_LEVEL_CRITICAL.format(self.temperature).encode(), ClientAPI.NOTIFY_MESSAGE_COLOR_DANGER)
                    elif dropped(80) or dropped(60) or dropped(40):
                        self.client.queue_notify(loc.HEAT_LEVEL.format(self.temperature).encode(), ClientAPI.NOTIFY_MESSAGE_COLOR_REGULAR)

                    if self.temperature < 10:
                        heat_pack = items.get("heat_pack")
                        if self.get_team().inventory.count_items(heat_pack) > 0:
                            if heat_pack.self_action(self):
                                self.client.queue_notify(
                                    loc.HEAT_PACK_AUTO_USED.encode(),
                                    ClientAPI.NOTIFY_MESSAGE_COLOR_BRIGHT)
                                self.remove_from_inventory(heat_pack, 1)
                do_sync_stats = True

        if do_sync_stats:
            self.client.sync_stats()

    def on_init(self, server_map: 'MapAPI'):
        self.update_state()

    def get_team(self) -> Optional['Team']:
        return self.client.get_team()

    def set_team(self, team: 'Team'):
        pass

    def get_client_id(self) -> int:
        return self.player_id

    def get_inventory(self) -> Optional[Inventory]:
        return self.client.get_inventory()

    def set_active_postponed_touch(self, touch: 'PostponedTouch'):
        super().set_active_postponed_touch(touch)
        self.reset_object_state()

    def kill(self):
        self.client.sync_stats()
        for c in MapAPI.instance.query_clients():
            if c == self.client:
                def do_notify():
                    c.queue_notify("You've been killed by {0}".format(self.last_damage_reason).encode()
                             if self.last_damage_reason else b"You've been killed",
                                   ClientAPI.NOTIFY_MESSAGE_COLOR_DANGER)
                MapAPI.instance.schedule_callback(do_notify, 500)
                if isinstance(c, Client):
                    c.schedule_respawn()
            else:
                if isinstance(self.last_damage_owner, PlayerObject):
                    if self.last_damage_owner.client == c:
                        c.queue_notify("You have killed {0}".format(
                            self.client.get_name().decode()
                        ).encode(), ClientAPI.NOTIFY_MESSAGE_COLOR_SUCCESS)
                    elif self.last_damage_reason:
                        c.queue_notify("{0} [{1}] {2}".format(
                            self.last_damage_owner.client.get_name().decode(),
                            self.last_damage_reason,
                            self.client.get_name().decode()
                        ).encode(), ClientAPI.NOTIFY_MESSAGE_COLOR_SUCCESS if self.client.is_enemy_to(c)
                                    else ClientAPI.NOTIFY_MESSAGE_COLOR_DANGER)
                    else:
                        c.queue_notify("{0} killed {1}".format(
                            self.last_damage_owner.client.get_name().decode(),
                            self.client.get_name().decode()
                        ).encode(), ClientAPI.NOTIFY_MESSAGE_COLOR_SUCCESS if self.client.is_enemy_to(c)
                                 else ClientAPI.NOTIFY_MESSAGE_COLOR_DANGER)
                else:
                    c.queue_notify("{0} was killed".format(
                        self.client.get_name().decode()
                    ).encode(), ClientAPI.NOTIFY_MESSAGE_COLOR_SUCCESS if self.client.is_enemy_to(c)
                             else ClientAPI.NOTIFY_MESSAGE_COLOR_DANGER)
        if isinstance(self.client, Client):
            self.client.player = None
        super().kill()

    def update_state(self):
        MAX_STATE_LENGTH = 20

        self.update_state_flags()
        if self.client.selected_record:
            item = self.client.selected_record.item.name
        else:
            item = loc.INVENTORY_NOTHING_TO_PLACE
        if len(item) > MAX_STATE_LENGTH:
            item = item[:MAX_STATE_LENGTH]
        if len(item) < MAX_STATE_LENGTH:
            item += " " * (MAX_STATE_LENGTH - len(item))
        if self.client.equipped_weapon:
            if self.client.weapon_state == WeaponState.WEAPON_RELOADING:
                weapon = loc.RELOADING
            else:
                if self.client.weapon_ammo == 0:
                    weapon = self.client.equipped_weapon.name + " " + loc.INVENTORY_WEAPON_EMPTY
                else:
                    weapon = self.client.equipped_weapon.name
            self.hit_auto = self.client.equipped_weapon.hit_auto
            self.hit_delay = self.client.equipped_weapon.hit_delay
        else:
            weapon = loc.INVENTORY_NOTHING_WEAPON
            self.hit_auto = 0
            self.hit_delay = 20
        if len(weapon) > MAX_STATE_LENGTH:
            weapon = weapon[:MAX_STATE_LENGTH]
        if len(weapon) < MAX_STATE_LENGTH:
            weapon += " " * (MAX_STATE_LENGTH - len(weapon))
        self.set_state(weapon, item)
        self.reset_object_state()

    def set_state(self, default_state: str, building_state: str):
        if self.client.get_control_object_id():
            return
        super().set_state(default_state, building_state)
        self.client.sync_stats()

    def on_healed(self):
        self.client.queue_notify(
            "Healed: {0}".format(self.health).encode(),
            ClientAPI.NOTIFY_MESSAGE_COLOR_BRIGHT)
        self.client.sync_stats()

    def on_heated(self):
        self.client.queue_notify(
            "Heat is being applied for {0}s".format(self.heat_time).encode(),
            ClientAPI.NOTIFY_MESSAGE_COLOR_BRIGHT)
        self.client.sync_stats()

    def on_damaged(self):
        self.client.queue_notify(
            "Damage received, health: {0}".format(self.health).encode(),
            ClientAPI.NOTIFY_MESSAGE_COLOR_DANGER, "damage", priority=True)
        self.client.sync_stats()

    def on_fall(self, server_map: 'MapAPI', fall_speed: int):
        if fall_speed <= PlayerObject.SAFE_FALL_SPEED:
            return

        self.damage((fall_speed - PlayerObject.SAFE_FALL_SPEED) * PlayerObject.FALL_DAMAGE_PER_SPEED, "fall")

    def serialize(self) -> Dict[bytes, bytes]:
        d = super().serialize()
        d.update({
            b"t": bool.to_bytes(self.tutorial, 1, "little")
        })
        return d

    def deserialize(self, data: Dict[bytes, bytes]):
        super().deserialize(data)
        if b"t" in data:
            self.tutorial = bool.from_bytes(data[b"t"], "little")

    @staticmethod
    def merge_add_to_inventory_msgs(ntf: QueuedNotification, ctx_obj: AddToInventoryCtx) -> bytes:
        ntf.context_object.amount += ctx_obj.amount
        ntf.context_object.total_amount += ctx_obj.amount
        return "+{0} {1}, total: {2}".format(
            ntf.context_object.amount, ctx_obj.item_name, ntf.context_object.total_amount).encode()

    def add_to_inventory(self, item: items.Item, amount: int, health: float, notify: bool = True):
        new_amount, new_record = self.get_team().inventory.add_item(item, amount, health)
        if notify:
            self.client.queue_notify(
                "+{0} {1}, total: {2}".format(amount, item.name, new_amount).encode(),
                ClientAPI.NOTIFY_MESSAGE_COLOR_REGULAR, "add_to_inventory_{0}".format(item.identity),
                context_obj=AddToInventoryCtx(amount, new_amount, item.name),
                merge_msg=PlayerObject.merge_add_to_inventory_msgs,
                delay=0.25, priority=True, trigger_immediately=False)
        upd = False
        if self.client.selected_record is None:
            self.client.selected_record = new_record
            upd = True
        if upd:
            self.update_state()

    def remove_record_from_inventory(self, record: InventoryRecord, remove_amount: int = 1) -> Union[bool, float]:
        remaining = self.get_team().inventory.remove_from_record(record, remove_amount)
        upd = False
        if remaining == 0 and self.client.selected_record is not None:
            if self.client.selected_record == record:
                self.client.selected_record = self.get_team().inventory.get_record(record.item)
                upd = True
        if upd:
            self.update_state()
        return remaining

    def on_aim(self, angle: int):
        self.angle = angle
        self.reset_object_state()

    def get_angle_state(self):
        if self.angle < 30:
            return 0
        if self.angle < 70:
            return 1
        if self.angle < 110:
            return 2
        if self.angle < 160:
            return 3
        return 4

    def get_object_state(self) -> int:
        if self.picking_state:
            return ObjectAPI.OBJECT_STATE_PICKING
        if self.active_postponed_touch:
            return super().get_object_state()
        if self.client.equipped_weapon:
            if self.client.equipped_weapon.hold_object_state:
                return self.client.equipped_weapon.hold_object_state[self.get_angle_state()]
        return super().get_object_state()

    def remove_from_inventory(self, item: items.Item, remove_amount: int = 1) -> Union[bool, float]:
        yielded_health, remaining = self.get_team().inventory.remove_item(item, remove_amount)
        if yielded_health == 0:
            return False
        upd = False
        if remaining == 0 and self.client.selected_record is not None:
            if self.client.selected_record.item == item:
                self.client.selected_record = self.get_team().inventory.get_record(item)
                upd = True
        if upd:
            self.update_state()
        return yielded_health

    def damage(self, value: int, reason: Optional[str] = None, owner: Optional['ObjectAPI'] = None) -> bool:
        if self.god or self.client.spawn_tutorial_god:
            return False
        self.client.force_query(None)
        if super().damage(value, reason=reason, owner=owner):
            if reason:
                MapAPI.instance.print("Damage {0}: {1}".format(value, reason))
            else:
                MapAPI.instance.print("Damage {0}, now at {1}".format(value, self.health))
            return True
        return False

    def heal(self, value: int) -> bool:
        if not super().heal(value):
            return False
        MapAPI.instance.print("Healed {0}, now at {1}".format(value, self.health))
        return True

    def on_hit(self, angle: int):
        if self.hit_cooldown and time.time() < self.hit_cooldown:
            return

        self.hit_cooldown = time.time() + 0.02 * self.hit_delay

        effect_left: bytes = None
        effect_right: bytes = None
        effect_offset = 1
        hit_value: int = 5
        looking_left: bool = self.looking_left()

        if self.client.equipped_weapon:
            # Check if weapon uses ammo system (hit_capacity > 0)
            if self.client.equipped_weapon.hit_capacity > 0:
                # Check if reloading
                if self.client.weapon_state == WeaponState.WEAPON_RELOADING:
                    return
                # Consume ammo
                if self.client.weapon_ammo > 0:
                    self.client.weapon_ammo -= 1
                else:
                    # Out of ammo, try to reload
                    if self.client.weapon_consume_refill():
                        # Start reloading
                        self.client.weapon_state = WeaponState.WEAPON_RELOADING
                        self.client.weapon_reloading_timer = time.time() + (self.client.equipped_weapon.hit_refill_time / 1000.0)
                        self.update_state()
                    else:
                        # No ammo crates available
                        self.client.queue_notify(
                            loc.INVENTORY_WEAPON_OUT_OF_AMMO.encode(),
                            ClientAPI.NOTIFY_MESSAGE_COLOR_DANGER)
                    return
                # Fire bullets if we have ammo
                for n in range(0, self.client.equipped_weapon.hit_multiple):
                    hit_range = self.client.equipped_weapon.hit_range
                    off = 0.5
                    xx = self.get_x() + math.sin(math.radians(angle + 90)) * (random.random() - 0.5) * 2.0 * hit_range
                    if self.looking_left():
                        xx -= off
                    else:
                        xx += off
                    yy = self.get_y() + math.cos(math.radians(angle + 90)) * (random.random() - 0.5) * 2.0 * hit_range
                    if isinstance(self.client.equipped_weapon.hit_effect, list):
                        e = random.choice(self.client.equipped_weapon.hit_effect)
                    else:
                        e = self.client.equipped_weapon.hit_effect
                    MapAPI.instance.add_bullet(
                        xx, yy, self.get_team_id(),
                        self.client.equipped_weapon.hit_value, angle,
                        self.client.equipped_weapon.hit_sound,
                        e)
                return
            elif self.client.equipped_weapon.hit_consumable:
                # Legacy consumable system
                if self.get_team().inventory.has_item(self.client.equipped_weapon.hit_consumable):
                    self.get_team().inventory.remove_item(self.client.equipped_weapon.hit_consumable, 1)
                for n in range(0, self.client.equipped_weapon.hit_multiple):
                    hit_range = self.client.equipped_weapon.hit_range
                    off = 0.5
                    xx = self.get_x() + math.sin(math.radians(angle + 90)) * (random.random() - 0.5) * 2.0 * hit_range
                    if self.looking_left():
                        xx -= off
                    else:
                        xx += off
                    yy = self.get_y() + math.cos(math.radians(angle + 90)) * (random.random() - 0.5) * 2.0 * hit_range
                    if isinstance(self.client.equipped_weapon.hit_effect, list):
                        e = random.choice(self.client.equipped_weapon.hit_effect)
                    else:
                        e = self.client.equipped_weapon.hit_effect
                    MapAPI.instance.add_bullet(
                        xx, yy, self.get_team_id(),
                        self.client.equipped_weapon.hit_value, angle,
                        self.client.equipped_weapon.hit_sound,
                        e)
                return
            else:
                self.client.queue_notify(
                    loc.INVENTORY_WEAPON_OUT_OF_AMMO.encode(),
                    ClientAPI.NOTIFY_MESSAGE_COLOR_DANGER)
                self.update_state()

        if self.client.equipped_weapon and self.client.equipped_weapon.hit_animations:
            effect_left = self.client.equipped_weapon.hit_animations[0]
            effect_right = self.client.equipped_weapon.hit_animations[1]
            effect_offset = self.client.equipped_weapon.hit_offset

        hit_distance = 4

        if self.client.equipped_weapon and self.client.equipped_weapon.hit_value:
            hit_value = self.client.equipped_weapon.hit_value
            hit_distance = self.client.equipped_weapon.hit_distance

        objs = MapAPI.instance.query_objects(int(self.get_x() - hit_distance), int(self.get_y() - 1), hit_distance + 2, 4) if looking_left else \
            MapAPI.instance.query_objects(int(self.get_x() - 2), int(self.get_y() - 1), hit_distance + 2, 4)

        degradation = 0

        for obj in objs:
            if obj.neutral:
                continue
            if obj.get_team() == self.get_team():
                continue
            obj.damage(hit_value)
            degradation += 1

        bases = MapAPI.instance.query_bases(int(self.get_x() - hit_distance), int(self.get_y() - 1), hit_distance + 2, 4) if looking_left else \
            MapAPI.instance.query_bases(int(self.get_x() - 2), int(self.get_y() - 1), hit_distance + 2, 4)

        for b in bases:
            if b.neutral:
                continue
            if b.team == self.get_team():
                continue
            b.damage(hit_value)

        effect = effect_left if looking_left else effect_right

        if effect:
            MapAPI.instance.send_effect(
                self.get_x() - effect_offset if looking_left else self.get_x() + effect_offset, self.get_y(),
                effect_left if looking_left else effect_right)

    def on_touch(self, x: int, y: int) -> Union[None, PostponedTouch]:
        return hit.touch(self, x, y)

    def on_query(self, q: bytes) -> Optional[QueryResponse]:
        from .. query import query
        return query(self, q, 4)


def allocate_player(player_id: int, c: ClientAPI):
    # noinspection PyTypeChecker
    return PlayerObject(player_id, c)


def allocate_client(client_id: int):
    return Client(client_id)
