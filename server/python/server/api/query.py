from typing import Optional, List, Callable, Union
from .. import loc


class QueryResponseOption(object):
    def icon(self) -> Union[int, bytes]:
        return 0

    def __bytes__(self):
        return self.__str__().encode("utf-8")

    def secondary(self) -> bool:
        return False

    def act(self, action: bytes) -> Optional['QueryResponse']:
        return None


class OPT(QueryResponseOption):
    def __init__(self, title: str, cb: Callable[[any], Optional['QueryResponse']] = None, icon: Union[int, bytes] = 0, secondary: bool = False):
        self.title = title
        self.cb = cb
        self._icon = icon
        self._secondary = secondary

    def __str__(self) -> str:
        return self.title

    def secondary(self) -> bool:
        return self._secondary

    def icon(self) -> Union[int, bytes]:
        return self._icon

    def act(self, action: bytes) -> Optional['QueryResponse']:
        if self.cb:
            return self.cb(action)
        return None


def NOACT(cb: Callable[[any], Optional['QueryResponse']], *args, **kwargs):
    def act(action: bytes):
        return cb(*args, **kwargs)
    return act


def ACT(cb: Callable[[bytes, any], Optional['QueryResponse']], *args, **kwargs):
    def act(action: bytes):
        return cb(action, *args, **kwargs)
    return act


class QueryResponse(object):
    FLAG_MESSAGE_TO_SIDE = 0x01

    def __init__(self, query: bytes, message: bytes, image: Optional[bytes] = None):
        self.query = query
        self.current: int = 0
        self.message = message
        self.edit: bool = False
        self.flags: int = 0
        self.options: Optional[List[QueryResponseOption]] = None
        self.description: Optional[bytes] = None
        self.actions: List[bytes] = []
        self.cancel_action: bytes = loc.EXIT.encode()
        self.image: Optional[bytes] = image

    def quick_cancel(self) -> bool:
        return True

    def cancelled(self) -> Optional['QueryResponse']:
        return None

    def selected(self, option: int, action: bytes) -> Optional['QueryResponse']:
        if self.options:
            if option < len(self.options):
                return self.options[option].act(action)
        return None


class DescriptionQueryResponse(QueryResponse):
    def __init__(self, title: bytes, description: bytes):
        super().__init__(b"", title)
        self.description = description
        self.actions = [loc.OK.encode()]


class NothingQueryResponse(QueryResponse):
    def __init__(self):
        super().__init__(b"", b"")
