from typing import Optional, Dict, List, Tuple, TYPE_CHECKING, Callable

from dataclasses import dataclass

from . query import QueryResponse
from .. loc.emails import EmailTemplate
from .. unboundapi import update_client_data

import random
import time

if TYPE_CHECKING:
    from .. team import Team
    from .. api.map import MapAPI
    from . object import ObjectAPI
    from . computer import ComputerAPI


@dataclass
class QueuedNotification:
    message: bytes
    color: int
    context: str
    context_object: any
    delay: float


class ClientAPI(object):
    NOTIFY_MESSAGE_COLOR_REGULAR = 0x07
    NOTIFY_MESSAGE_COLOR_BRIGHT = 0x07 + 0x40
    NOTIFY_MESSAGE_COLOR_DANGER = 0x02 + 0x40
    NOTIFY_MESSAGE_COLOR_WARNING = 0x06 + 0x40
    NOTIFY_MESSAGE_COLOR_SUCCESS = 0x04 + 0x40

    MODULE_INTRO = b"INTRO"

    def __init__(self, client_id: int):
        self.client_id = client_id
        self.authenticated = False
        self.action_handlers: Dict[bytes, Callable[[Optional[bytes]], None]] = {}
        self.action_handlers_once: Dict[bytes, Callable[[Optional[bytes]], None]] = {}
        self._team: Optional['Team'] = None
        self.music_enabled = True
        self._music_name = None
        self.notification_queue: List[QueuedNotification] = []
        self.blocked_notifications = set()
        self.notification_delay = None
        self.active_notification: Optional[QueuedNotification] = None
        self._music_list = []
        self.terminal_context: Dict[str, object] = {}

    def on_destroyed(self):
        from . map import MapAPI
        if self.has_team():
            self.get_team().remove_member(self)
        MapAPI.instance.deadlines.update()

    def set_team(self, team: 'Team'):
        self._team = team
        self.on_team_set(team.team_id if team else 0)

    def get_team(self) -> Optional['Team']:
        return self._team

    def has_team(self) -> bool:
        return self._team is not None

    def on_terminal(self, command: bytes):
        pass

    def on_update(self, api: 'MapAPI'):
        self.deliver_notifications()

    def stop_music(self):
        if self._music_name:
            self.module_action(self._music_name, {b"e": b"\0"})
            self._music_name = None

    def get_current_music_name(self) -> str:
        if not self._music_name:
            return "Stopped"
        m = self.get_module_prop_str(self._music_name, b"TITLE")
        if m is None:
            return "Unknown"
        return m.decode()

    def get_current_music_link(self) -> Optional[str]:
        if not self._music_name:
            return None
        m = self.get_module_prop_str(self._music_name, b"LINK")
        if m is None:
            return None
        return m.decode()

    def add_email(self, subject: str, bodies: List[str], image: bytes = None,
                  action_name: bytes = None, action=None):
        pass

    def add_email_template(self, template: EmailTemplate, custom_image=None,
                           action_name: bytes = None, action=None, **kwargs):
        self.add_email(template.subject.format(**kwargs), [
            body.format(**kwargs)
            for body in template.bodies
        ], custom_image or template.image, action_name=action_name, action=action)

    def play_music(self, name: bytes, notify: bool = True):
        if self._music_name:
            self.stop_music()
        self.push_module(name)
        if notify:
            title = self.get_module_prop_str(name, b"TITLE")
            if title is None:
                return

            self.module_action(name, {b"e": b"\1"})
            self.queue_notify(
                "Playing: {0}".format(title.decode()).encode(),
                ClientAPI.NOTIFY_MESSAGE_COLOR_REGULAR, "player")
        else:
            self.module_action(name, {b"e": b"\1"})
        self._music_name = name

    def clear_playlist(self):
        self._music_list = []

    def play_playlist(self):
        from .. api.map import MapAPI

        if not self.music_enabled:
            return

        # trigger to play music after it is done
        self.handle_action_once(b"music", self.play_playlist)

        def do_play():
            if not self._music_list:
                def is_music(name: bytes):
                    return name.startswith(b"MUSIC_")
                self._music_list = list(filter(is_music, self.list_modules()))
                random.shuffle(self._music_list)
            music = self._music_list.pop()
            if music is None:
                return
            self.play_music(music)

        if self._music_name:
            self.stop_music()
            MapAPI.instance.schedule_callback(do_play, 200)
        else:
            do_play()

    def on_action(self, action: bytes, payload: Optional[bytes]):
        if action in self.action_handlers_once:
            a = self.action_handlers_once[action]
            del self.action_handlers_once[action]
            a(payload)
        elif action in self.action_handlers:
            self.action_handlers[action](payload)
        else:
            from .. api import MapAPI
            MapAPI.instance.print("Note: unhandled action {0}".format(action.decode()))

    def handle_action_once(self, action: bytes, cb: Callable[[], None]):
        def handle(ignore: Optional[bytes]):
            cb()
        self.action_handlers_once[action] = handle

    def handle_action_payload(self, action: bytes, cb: Callable[[Optional[bytes]], None]):
        self.action_handlers[action] = cb

    def remove_action_handle(self, action: bytes):
        if action in self.action_handlers_once:
            del self.action_handlers_once[action]
        if action in self.action_handlers:
            del self.action_handlers[action]

    def serialize(self) -> Dict:
        return {
            b"team": self._team.name.encode() if self._team else b"",
            b"music": b"yes" if self.music_enabled else b"no",
            b"terminal_context": self.terminal_context,
        }

    def is_enemy_to(self, o: 'ClientAPI') -> bool:
        return self._team != o._team

    def deserialize(self, data: Dict):
        from .. team import find_team
        self.set_team(find_team(data[b"team"]))
        self.music_enabled = data.get(b"music", b"yes") == b"yes"
        self.terminal_context = data.get(b"terminal_context", {})

    def congratulate(self, messages: List[bytes], stats: List[Tuple[bytes, int, int]]):
        pass

    def computer_session(self, computer: 'ComputerAPI', on_closed: Callable):
        pass

    def send_chat_message(self, message: bytes):
        pass

    def get_client_object(self) -> Optional['ObjectAPI']:
        from . map import MapAPI
        c = self.get_client_object_id()
        if not c:
            return None
        return MapAPI.instance.get_object(c)

    def get_control_object(self) -> Optional['ObjectAPI']:
        from . map import MapAPI
        c = self.get_control_object_id()
        if not c:
            return None
        return MapAPI.instance.get_object(c)

    def block_notifications(self, t: bytes):
        if t not in self.blocked_notifications:
            self.blocked_notifications.add(t)

    def unblock_notifications(self, t: bytes):
        if t in self.blocked_notifications:
            self.blocked_notifications.remove(t)

    def queue_notify(self, msg: bytes, color: int, context: str = None, context_obj = None,
                     merge_msg: Callable[[QueuedNotification, any], bytes] = None,
                     delay: float = 2, priority: bool = False, trigger_immediately: bool = True):
        notification = QueuedNotification(msg, color, context, context_obj, delay)

        if context:
            for i, q in enumerate(self.notification_queue):
                if q.context == context:
                    q.color = color
                    if merge_msg:
                        q.message = merge_msg(q, context_obj)
                    else:
                        q.message = msg
                    q.delay = delay
                    if color == ClientAPI.NOTIFY_MESSAGE_COLOR_DANGER:
                        self.notification_queue.pop(i)
                        self.interrupt_notification(q)
                    return
        was_empty = not self.notification_queue
        if color == ClientAPI.NOTIFY_MESSAGE_COLOR_DANGER:
            self.interrupt_notification(notification)
            return
        if priority:
            self.notification_queue.insert(0, notification)
        else:
            self.notification_queue.append(notification)
        if trigger_immediately:
            self.deliver_notifications()
        else:
            if was_empty:
                self.notification_delay = time.time() + 0.05

    def interrupt_notification(self, notification: QueuedNotification):
        now = time.time()
        if self.notification_delay and now >= self.notification_delay:
            self.active_notification = None

        if self.active_notification and self.active_notification.color != ClientAPI.NOTIFY_MESSAGE_COLOR_DANGER:
            remaining_delay = max(0, (self.notification_delay or now) - now)
            active_notification = QueuedNotification(
                self.active_notification.message,
                self.active_notification.color,
                self.active_notification.context,
                self.active_notification.context_object,
                remaining_delay)
            self.notification_queue.insert(0, active_notification)

        self.notify(notification.message, notification.color)
        self.active_notification = notification
        self.notification_delay = now + notification.delay

    def deliver_notifications(self):
        if self.blocked_notifications:
            return
        if self.notification_delay:
            if time.time() < self.notification_delay:
                return
            self.active_notification = None
        if not self.notification_queue:
            return
        n = self.notification_queue.pop(0)
        if not n:
            return
        self.notify(n.message, n.color)
        self.active_notification = n
        self.notification_delay = time.time() + n.delay

    def update_user_data(self, data: dict):
        update_client_data(self.get_user_id().decode(), data)

    # do not override the following
    def notify(self, msg: bytes, color: int): ...
    def disconnect(self): ...
    def sync_stats(self): ...
    def force_query(self, response: Optional[QueryResponse]): ...
    def force_watch(self, x: int, y: int): ...
    def get_user_id(self) -> bytes: ...
    def get_name(self) -> bytes: ...
    def push_module(self, name: bytes): ...
    def push_screen(self, name: bytes): ...
    def push_memory(self, addr: int, data: bytes): ...
    def get_module_prop_int(self, module_name: bytes, key: bytes, default: int) -> int: ...
    def get_module_prop_str(self, module_name: bytes, key: bytes) -> Optional[bytes]: ...
    def list_modules(self) -> Tuple[bytes]: ...
    def module_action(self, name: bytes, data: Dict[bytes, bytes]): ...
    def set_object_control(self, object_id: int): ...
    def get_client_object_id(self) -> int: ...
    def get_control_object_id(self) -> int: ...
    def on_team_set(self, team_id: int): ...
