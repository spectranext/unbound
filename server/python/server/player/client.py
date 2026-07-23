from typing import Optional, List, Callable, Tuple, TYPE_CHECKING, Dict
from enum import Enum

from . terminal import apply_terminal_command

from . email import EmailInbox
from .. api.client import ClientAPI
from .. api.computer import ComputerAPI
from .. api.map import MapAPI
from .. api.query import QueryResponse
from .. api.object import ObjectAPI
from .. scenarios import get_scenario
from .. inventory import Inventory, InventoryRecord
from .. import loc

if TYPE_CHECKING:
    from .. bases import BaseItem
    from .. items import Item
    from . import PlayerObject


class AdminRole(object):
    USER = 0
    MOD = 1
    ADMIN = 2


class WeaponState(Enum):
    WEAPON_NORMAL = 0
    WEAPON_RELOADING = 1


class Client(ClientAPI):
    def __init__(self, client_id: int):
        super().__init__(client_id)
        scenario = get_scenario(MapAPI.instance.scenario)
        self.tutorial: bool = not scenario.tutorial
        self.role: int = 2 if scenario.auto_login else 0
        self.email_inbox = EmailInbox()
        self.inventory = Inventory()
        self.selected_record: Optional[InventoryRecord] = None
        self.equipped_weapon: Optional['Item'] = None
        self.weapon_ammo: int = 0
        self.weapon_state: WeaponState = WeaponState.WEAPON_NORMAL
        self.weapon_reloading_timer: Optional[float] = None
        self.player: Optional['PlayerObject'] = None
        self.welcome_email_sent: bool = False
        # Invulnerability during first status/tutorial after new-client ship spawn (not manual menu tutorial).
        self.spawn_tutorial_god: bool = False

    def get_inventory(self) -> Inventory:
        return self.inventory

    def get_selected_record(self) -> Optional[InventoryRecord]:
        return self.selected_record

    def is_reloading(self) -> bool:
        return self.weapon_state == WeaponState.WEAPON_RELOADING

    def equip_weapon(self, record: 'InventoryRecord', inventory: Inventory):
        if self.equipped_weapon is not None:
            # put equipped item back
            inventory.add_item(self.equipped_weapon, 1, 1.)
        self.equipped_weapon = record.item
        if self.equipped_weapon and self.equipped_weapon.hit_capacity > 0:
            self.weapon_ammo = self.equipped_weapon.hit_capacity
        else:
            self.weapon_ammo = 0
        self.weapon_state = WeaponState.WEAPON_NORMAL
        self.weapon_reloading_timer = None
        inventory.remove_from_record(record, 1)

    def select_record(self, record: 'InventoryRecord', inventory: Inventory):
        from .. import loc
        
        player = self.get_client_object()
        if not player:
            return
        
        if record.item.is_weapon():
            self.equip_weapon(record, inventory)
        else:
            if record.item.selectable:
                self.selected_record = record
            else:
                if record.item.self_action:
                    if record.item.self_action(player):
                        player.remove_record_from_inventory(record, 1)
                        return
                self.notify(
                    loc.YOU_CANNOT_SELECT.format(record.item.name).encode(),
                    ClientAPI.NOTIFY_MESSAGE_COLOR_DANGER)
                return
        player.update_state()

    def weapon_consume_refill(self) -> bool:
        """Try to refill weapon ammo from team inventory. Returns True if successful."""
        if not self.equipped_weapon or not self.equipped_weapon.hit_refill_item:
            return False
        
        team = self.get_team()
        if not team:
            return False
        
        refill_item = self.equipped_weapon.hit_refill_item
        refill_amount = self.equipped_weapon.hit_refill_amount
        
        if team.inventory.count_items(refill_item) >= refill_amount:
            team.inventory.remove_item(refill_item, refill_amount)
            self.weapon_ammo = self.equipped_weapon.hit_capacity
            return True
        return False

    def on_update(self, api: 'MapAPI'):
        super().on_update(api)
        
        if self.weapon_state == WeaponState.WEAPON_RELOADING and self.weapon_reloading_timer is not None:
            import time
            if time.time() >= self.weapon_reloading_timer:
                # Reloading complete
                self.weapon_state = WeaponState.WEAPON_NORMAL
                self.weapon_reloading_timer = None
                player = self.get_client_object()
                if player:
                    player.update_state()

    def get_state_flags(self) -> int:
        """Override to add flashing when reloading."""
        flags = 0
        if self.weapon_state == WeaponState.WEAPON_RELOADING:
            flags |= ObjectAPI.STATE_FLAG_FLASHING
        return flags

    def on_destroyed(self):
        super().on_destroyed()
        MapAPI.instance.cache_client(self)

    def on_terminal(self, command: bytes):
        try:
            res = apply_terminal_command(self, command.decode())
        except Exception as e:
            self.queue_notify("Error: {0}".format(str(e)).encode(), ClientAPI.NOTIFY_MESSAGE_COLOR_DANGER, priority=True)
        else:
            self.queue_notify(res.encode(), ClientAPI.NOTIFY_MESSAGE_COLOR_BRIGHT)

    def serialize(self) -> Dict:
        d = super().serialize()
        d["tutorial"] = self.tutorial
        d[b"welcome_email_sent"] = self.welcome_email_sent
        d[b"I"] = self.inventory.serialize()
        if self.equipped_weapon:
            d[b"equipped_weapon"] = self.equipped_weapon.identity.encode()
            d[b"weapon_ammo"] = self.weapon_ammo.to_bytes(2, "little")
            d[b"weapon_state"] = self.weapon_state.value.to_bytes(1, "little")
        player = self.get_client_object()
        if player:
            d[b"player"] = player.serialize()
            d[b"x"] = int(player.get_x())
            d[b"y"] = int(player.get_y())
        return d

    def deserialize(self, data: Dict):
        from .. items import Item
        
        super().deserialize(data)
        if b"tutorial" in data:
            self.tutorial = bool(data[b"tutorial"])
        if b"welcome_email_sent" in data:
            self.welcome_email_sent = bool(data[b"welcome_email_sent"])
        if b"I" in data:
            self.inventory.deserialize(data[b"I"])
        if b"equipped_weapon" in data:
            weapon_identity = data[b"equipped_weapon"].decode()
            if weapon_identity in Item.ITEMS:
                self.equipped_weapon = Item.ITEMS[weapon_identity]
                if b"weapon_ammo" in data:
                    self.weapon_ammo = int.from_bytes(data[b"weapon_ammo"], "little")
                else:
                    # Backward compatibility: set to capacity if available
                    if self.equipped_weapon and self.equipped_weapon.hit_capacity > 0:
                        self.weapon_ammo = self.equipped_weapon.hit_capacity
                if b"weapon_state" in data:
                    self.weapon_state = WeaponState(int.from_bytes(data[b"weapon_state"], "little"))
        if not self.has_team():
            MapAPI.instance.print("Warning: team is None")

    def add_email(self, subject: str, bodies: List[str], image: bytes = None,
                  action_name: bytes = None, action: Callable[[], Optional[QueryResponse]] = None):
        self.email_inbox.add_email(subject, bodies, image, action_name, action)
        self.queue_notify(
            loc.EMAIL_YOU_HAVE_NEW.format(self.email_inbox.count_unread()).encode(),
            ClientAPI.NOTIFY_MESSAGE_COLOR_SUCCESS, "new_email", trigger_immediately=False)

    def base_installed(self, item: 'BaseItem', x: int, y: int):
        from .. contract.events import BasePlaced
        from .. items import get_item

        self.queue_notify(loc.PLAYER_BASE_PLACED.format(item.name).encode(), ClientAPI.NOTIFY_MESSAGE_COLOR_BRIGHT)
        if item == get_item("fob"):
            if self.get_team():
                self.get_team().fob_placed = True

        self.get_team().contract_event(BasePlaced(item))

    def respawn(self):
        if self.get_client_object():
            return

        class RespawnQuery(QueryResponse):
            def __init__(self, c: Client):
                super().__init__(b"", loc.PLAYER_RESPAWN.encode())
                self.c = c
                self.description = loc.PLAYER_RESPAWN_DESC.encode()
                self.actions = [loc.YES.encode()]
                self.cancel_action = loc.DISCONNECT.encode()

            def selected(self, option: int, action: bytes) -> Optional['QueryResponse']:
                from .. newclient import new_client
                self.c.play_playlist()
                new_client(self.c, b"respawn")
                return None

            def quick_cancel(self) -> bool:
                return False

            def cancelled(self) -> Optional['QueryResponse']:
                self.c.disconnect()
                return None

        self.force_query(RespawnQuery(self))

    def hiring_screen(self):
        self.block_notifications(b"hiring")
        self.push_module(b"HIRING")

        def music_done():
            pass

        messages = [
            "CONTRACT HOLDER {0} PRODUCTIVITY: INSUFFICIENT.".format(self.get_name().decode()).encode(),
            b"Replacement candidate screening in progress...",
        ]

        def shown_all_messages():
            self.module_action(b"HIRING", {b"s": b"e"})
            self.unblock_notifications(b"hiring")
            MapAPI.instance.schedule_callback(self.respawn, 1000)

        def push_line():
            next_message = messages.pop(0)

            def line_done():
                if len(messages):
                    MapAPI.instance.schedule_callback(push_line, 500)
                else:
                    MapAPI.instance.schedule_callback(shown_all_messages, 500)

            self.handle_action_once(b"done", line_done)
            self.module_action(b"HIRING", {b"s": b"l", b"l": next_message})

        def music():
            # show screen
            self.handle_action_once(b"music", music_done)
            self.play_music(b"SAD", notify=False)
            MapAPI.instance.schedule_callback(push_line, 5000)

        def screen():
            self.stop_music()
            self.push_screen(b"hiring")
            MapAPI.instance.schedule_callback(music, 1000)

        def show():
            self.module_action(b"HIRING", {b"s": b"c"})
            MapAPI.instance.schedule_callback(screen, 200)

        MapAPI.instance.schedule_callback(show, 500)

    def schedule_respawn(self):
        scenario = get_scenario(MapAPI.instance.scenario)
        if scenario.respawn:
            def do_respawn():
                from .. newclient import new_client
                self.play_playlist()
                new_client(self, b"respawn")

            MapAPI.instance.schedule_callback(do_respawn, 1000)
            return

        MapAPI.instance.schedule_callback(self.hiring_screen, 5000)

    def congratulate(self, messages: List[bytes], stats: List[Tuple[bytes, int, int]]):
        self.block_notifications(b"congrats")
        self.push_module(b"CONGRATS")
        self.push_screen(b"congrats")

        def music_done():
            pass

        self.handle_action_once(b"music", music_done)
        self.play_music(b"COMPLETE", notify=False)

        def shown_all_stats():
            self.module_action(b"CONGRATS", {b"s": b"e"})
            self.play_playlist()
            self.unblock_notifications(b"congrats")

        def push_stat():
            next_stat, next_stat_color, offset = stats.pop(0)

            def stat_done():
                if len(stats):
                    MapAPI.instance.schedule_callback(push_stat, 5)
                else:
                    MapAPI.instance.schedule_callback(shown_all_stats, 5000)

            self.handle_action_once(b"done", stat_done)
            self.module_action(
                b"CONGRATS", {
                    b"s": b"s",
                    b"l": next_stat,
                    b"o": int(offset).to_bytes(1, "little"),
                    b"c": int(next_stat_color).to_bytes(1, "little")
                })

        def shown_all_messages():
            self.handle_action_once(b"done", push_stat)
            # clear the screen
            self.module_action(b"CONGRATS", {b"s": b"c"})

        def push_line():
            next_message = messages.pop(0)

            def line_done():
                if len(messages):
                    MapAPI.instance.schedule_callback(push_line, 500)
                else:
                    MapAPI.instance.schedule_callback(shown_all_messages, 500)

            self.handle_action_once(b"done", line_done)
            self.module_action(b"CONGRATS", {b"s": b"l", b"l": next_message})

        MapAPI.instance.schedule_callback(push_line, 2000)

    def computer_session(self, computer: ComputerAPI, on_closed: Callable, info: bool = True, first_session: bool = True):
        if not computer.is_powered_on():
            return

        def dump_memory():
            self.push_memory(0x4000, computer.get_memory(0x4000, 6912))

        if not computer.session_join(self.client_id):
            return

        border = computer.get_ula()
        self.push_module(b"COMPUTER")

        def close_session():
            computer.session_leave(self.client_id)
            self.remove_action_handle(b"keyboard")
            self.module_action(b"COMPUTER", {b"s": b"e"})
            if on_closed:
                on_closed()

        def key_stroke(payload: bytes):
            if len(payload) != 2:
                return
            row = int(payload[0])
            data = int(payload[1]) & 0b11111

            computer.set_key(row, data)

            if row == 0x7f and data & 0b11 == 0:
                # both sym and space are pressed
                close_session()

        def computer_done():
            self.module_action(b"COMPUTER", {b"s": b"s"})

        def cleared():
            self.handle_action_payload(b"keyboard", key_stroke)
            dump_memory()
            # start the action
            MapAPI.instance.schedule_callback(computer_done, 500)

        self.handle_action_once(b"done", cleared)
        # clear the screen
        self.module_action(b"COMPUTER", {b"s": b"c", b"b": border.to_bytes(1, "little")})
