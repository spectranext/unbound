from typing import Callable, Optional

from .. api.object import ObjectAPI
from .. api.query import QueryResponse
from .. api.map import MapAPI


MODULE_NAME = b"QUERY"

QUERY_MODULE_BEGIN = 1
QUERY_MODULE_OPTION = 2
QUERY_MODULE_COMPLETE = 3
QUERY_MODULE_CLOSE = 4


def _u8(value: int) -> bytes:
    return int(value).to_bytes(1, "little")


def _icon_payload(icon) -> bytes:
    if isinstance(icon, bytes):
        return icon
    return (int(icon) & 0xFF).to_bytes(1, "little")


class QuerySession:
    def __init__(self, owner: Optional[ObjectAPI], client):
        self.owner = owner
        self.client = client
        self.current: Optional[QueryResponse] = None

    def query(self, q: bytes, create_response: Callable[[], Optional[QueryResponse]]):
        self._replace(create_response())

    def force(self, response: Optional[QueryResponse]):
        self._replace(response)

    def option(self, option: int, action: bytes):
        if self.current is None:
            MapAPI.instance.print("QuerySession option ignored; no current query")
            return
        MapAPI.instance.print("QuerySession option {0} action {1}".format(option, action))
        if action:
            response = self.current.selected(option, action)
        else:
            response = self.current.cancelled()
        MapAPI.instance.print("QuerySession option response {0}".format(
            response.__class__.__name__ if response is not None else "None"))
        self._replace(response)

    def _replace(self, response: Optional[QueryResponse]):
        previous = self.current
        self.current = response
        if response is None:
            if previous is not None:
                self._close()
        else:
            self._open(response)

    def _open(self, response: QueryResponse):
        MapAPI.instance.print("QuerySession open {0}".format(response.__class__.__name__))
        self.client.block_notifications(b"query")
        self.client.push_module(MODULE_NAME)
        if self.owner is not None:
            self.owner.set_object_state(ObjectAPI.OBJECT_STATE_CONTROL)

        self._send_response(response)

    def _close(self):
        self.client.unblock_notifications(b"query")
        self.client.module_action(MODULE_NAME, {b"t": _u8(QUERY_MODULE_CLOSE)})
        if self.owner is not None:
            self.owner.reset_object_state()

    def _send_response(self, response: QueryResponse):
        options = response.options or []
        actions = {
            bytes([ord("a")]) + str(i).encode(): action
            for i, action in enumerate(response.actions)
        }

        has_primary = any(not option.secondary() for option in options)
        has_secondary = any(option.secondary() for option in options)
        use_secondary = has_primary and has_secondary

        payload = {
            b"t": _u8(QUERY_MODULE_BEGIN),
            b"m": response.message,
            b"x": response.cancel_action,
            b"c": _u8(response.current),
            b"s": _u8(1 if use_secondary else 0),
            b"e": _u8(1 if response.edit else 0),
            b"q": _u8(1 if response.quick_cancel() else 0),
            b"f": _u8(response.flags),
            **actions,
        }
        if response.description is not None:
            payload[b"d"] = response.description
        if response.image is not None:
            payload[b"I"] = response.image
        self.client.module_action(MODULE_NAME, payload)

        for i, option in enumerate(options):
            self.client.module_action(MODULE_NAME, {
                b"t": _u8(QUERY_MODULE_OPTION),
                b"i": _u8(i),
                b"c": _icon_payload(option.icon()),
                b"s": _u8(1 if use_secondary and option.secondary() else 0),
                b"o": bytes(option),
            })

        self.client.module_action(MODULE_NAME, {b"t": _u8(QUERY_MODULE_COMPLETE)})
