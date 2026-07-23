import abc
from typing import Optional, Generator, Callable


class YieldAndClose(object):
    def __init__(self, data: bytes):
        self.data = data


class DeviceAPISession(object):
    def __init__(self, device: 'DeviceAPI'):
        self.device = device
        self.buffer = bytearray()
        self.g = None

    def start_session(self, on_device_session: Callable[['DeviceAPISession'], Generator[bytes, bytes, None]]):
        self.g = on_device_session(self)
        self.on_line(None)

    def on_closed(self):
        self.g.close()
        self.g = None

    def on_line(self, line: Optional[bytes]):
        try:
            response = self.g.send(line)
        except StopIteration:
            self.close()
        else:
            if response:
                if isinstance(response, YieldAndClose):
                    self.write_line(response.data)
                    self.close()
                else:
                    self.write_line(response)

    def on_data(self, data: bytes):
        self.buffer += data
        if b"\n" not in self.buffer:
            return
        b = self.buffer.split(b"\n")
        for line in b[:-1]:
            self.on_line(line.strip(b"\r"))
        self.buffer = b[-1]

    def write_line(self, line: bytes):
        line += b"\n"
        self.write_data(line)

    def write_data(self, data: bytes): ...
    def close(self): ...


class DeviceAPIHandler(object, metaclass=abc.ABCMeta):
    @abc.abstractmethod
    def on_device_session(self, session: DeviceAPISession) -> Generator[bytes, bytes, None]:
        pass


class DeviceAPI(object):
    def __init__(self):
        pass

    def session_new(self) -> DeviceAPISession:
        return DeviceAPISession(self)

    def listen(self, handler: DeviceAPIHandler): ...
    def listen_close(self): ...

    def connect_to(
        self, hostname: bytes, port: int,
        session: Callable[['DeviceAPISession'], Generator[bytes, bytes, None]]) -> Optional[DeviceAPISession]: ...

    def get_hostname(self) -> bytes: ...
    def set_hostname(self, hostname: bytes): ...
    def post_message(self, message: bytes): ...
    def get_device_id(self) -> int: ...
    def destroy(self): ...
