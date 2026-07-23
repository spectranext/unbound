import asyncio
import time
from typing import Optional

from ..api.client import ClientAPI
from ..api.map import MapAPI


MAX_CHAT_HISTORY = 100
MAX_CHAT_MESSAGE_LENGTH = 400


class ChatHub:
    def __init__(self):
        self.listeners = {}
        self.history = []
        self.lock = asyncio.Lock()

    async def subscribe(self, session_id: str, emit_message):
        async with self.lock:
            self.listeners[session_id] = emit_message
            return list(self.history)

    async def unsubscribe(self, session_id: str):
        async with self.lock:
            self.listeners.pop(session_id, None)

    async def broadcast(self, message: dict):
        async with self.lock:
            self.history.append(message)
            if len(self.history) > MAX_CHAT_HISTORY:
                self.history = self.history[-MAX_CHAT_HISTORY:]
            listeners = list(self.listeners.items())

        stale_session_ids = []
        for session_id, emit_message in listeners:
            try:
                await emit_message(message)
            except Exception:
                stale_session_ids.append(session_id)

        if not stale_session_ids:
            return

        async with self.lock:
            for session_id in stale_session_ids:
                self.listeners.pop(session_id, None)


CHAT_HUB = ChatHub()
CHAT_LOOP: Optional[asyncio.AbstractEventLoop] = None


def register_chat_loop():
    global CHAT_LOOP
    CHAT_LOOP = asyncio.get_running_loop()


def normalize_chat_text(text) -> str | None:
    if not isinstance(text, str):
        return None

    normalized = " ".join(text.strip().split())
    if not normalized:
        return None

    return normalized[:MAX_CHAT_MESSAGE_LENGTH]


def build_chat_message(name: str, text: str) -> dict:
    return {
        "type": "chat_message",
        "name": name,
        "text": text,
        "sent_at": int(time.time()),
    }


def broadcast_to_players(name: str, text: str):
    line = f"{name}: {text}".encode("utf-8")
    for client in MapAPI.instance.query_clients():
        client.send_chat_message(line)
        client.queue_notify(line, ClientAPI.NOTIFY_MESSAGE_COLOR_REGULAR)


async def publish_chat_message(name: str, text: str):
    register_chat_loop()
    message = build_chat_message(name, text)
    broadcast_to_players(name, text)
    await CHAT_HUB.broadcast(message)
    return message


def publish_chat_message_to_web(name: str, text: str):
    if CHAT_LOOP is None or CHAT_LOOP.is_closed():
        return

    try:
        asyncio.run_coroutine_threadsafe(
            CHAT_HUB.broadcast(build_chat_message(name, text)),
            CHAT_LOOP,
        )
    except RuntimeError:
        pass
