from typing import Optional, List, Set, Callable, Protocol, Tuple, Union
from . spectranet import SpectranetConfiguration, SpectranetConfigurationOption
from .. imagegen import Image


MountedXFSDirectoryEntry = Union[
    bytes,
    str,
    Tuple[bytes, bool],
    Tuple[str, bool],
    Tuple[bytes, bool, int],
    Tuple[str, bool, int],
]


class MountedXFSFile(Protocol):
    def read(self, size: int) -> Union[bytes, str, None]: ...
    def write(self, data: bytes) -> Union[int, None]: ...
    def seek(self, mode: int, offset: int) -> int: ...
    def close(self): ...


class MountedXFSFolder(Protocol):
    def readdir(self) -> Optional[MountedXFSDirectoryEntry]: ...
    def close(self): ...


MountedXFSObject = Union[MountedXFSFile, MountedXFSFolder]
MountedXFSFactory = Callable[[bytes, int], MountedXFSObject]


class ComputerAPI(object):
    MEMORY_OFFSET_SPECTRANET_RAM = 16 * 4096
    MEMORY_OFFSET_SPECTRANET_ROM = 48 * 4096
    MEMORY_OFFSET_SPECTRANET_FS_CONFIG = MEMORY_OFFSET_SPECTRANET_ROM + 31 * 4096
    MEMORY_OFFSET_SPECTRANET_FS_SIZE = 4096

    def __init__(self):
        self.event_subscribers: Set[Callable[[Optional[bytes]], None]] = set()
        self.last_events: Optional[List[bytes]] = list()
        self.log_subscribers: Set[Callable[[bytes], None]] = set()
        self.last_log_messages: List[bytes] = list()

    def subscribe_events(self, cb: Callable[[Optional[bytes]], None]):
        self.event_subscribers.add(cb)

    def unsubscribe_events(self, cb: Callable[[Optional[bytes]], None]):
        self.event_subscribers.remove(cb)

    def notify_event(self, ev: Optional[bytes]):
        for e in self.event_subscribers:
            e(ev)
        if ev:
            self.last_events.append(ev)
            if len(self.last_events) > 5:
                self.last_events = self.last_events[-5:]
        else:
            self.last_events = list()
            self.event_subscribers = None

    def subscribe_logs(self, cb: Callable[[bytes], None]):
        self.log_subscribers.add(cb)

    def unsubscribe_logs(self, cb: Callable[[bytes], None]):
        self.log_subscribers.remove(cb)

    def notify_log_message(self, msg: bytes):
        for e in self.log_subscribers:
            e(msg)
        self.last_log_messages.append(msg)
        if len(self.last_log_messages) > 100:
            self.last_log_messages = self.last_log_messages[-100:]

    def get_spectranet_fs_configuration(self) -> SpectranetConfiguration:
        mem = self.get_memory(ComputerAPI.MEMORY_OFFSET_SPECTRANET_FS_CONFIG,
                              ComputerAPI.MEMORY_OFFSET_SPECTRANET_FS_SIZE)
        return SpectranetConfiguration(mem)

    def set_spectranet_fs_configuration(self, c: SpectranetConfiguration):
        self.set_memory(ComputerAPI.MEMORY_OFFSET_SPECTRANET_FS_CONFIG, c.bake())

    def set_tnfs(self, address: bytes, autoboot: bool):
        sp_config = self.get_spectranet_fs_configuration()
        am_config_section = sp_config.obtain_section(0x01ff)
        am_config_section[0x00] = SpectranetConfigurationOption("string", address)
        am_config_section[0x81] = SpectranetConfigurationOption("byte", 1 if autoboot else 0)
        self.set_spectranet_fs_configuration(sp_config)

    def get_image(self) -> Optional[bytes]:
        if not self.is_powered_on():
            return None

        memory = self.get_memory(0x4000, 6912)
        border = self.get_ula()

        def get_pixel(x_, y_):
            x1 = (x_ // 8) & 0b11111
            y1 = (y_ & 0b111) << 8
            y2 = (y_ & 0b111000) << 2
            y3 = (y_ & 0b11000000) << 5
            address = x1 | y1 | y2 | y3
            return memory[address] & (1 << (x_ % 8))

        def get_color(x_, y_):
            address = 6144 + (y_ * 32) + x_
            return memory[address]

        im = Image(w=12, h=8)

        for x in range(0, 10):
            im.set_color(x + 2, 0, border | (border << 3))
            im.set_color(x + 2, 7, border | (border << 3))

        for y in range(1, 7):
            im.set_color(2, y, border | (border << 3))
            im.set_color(11, y, border | (border << 3))

        for y in range(0, 48):
            for x in range(0, 64):
                _count = 0
                for yy in range(0, 4):
                    for xx in range(0, 4):
                        if get_pixel(x * 4 + xx, y * 4 + yy):
                            _count += 1
                im.set_pixel(x + 3 * 8, y + 8, _count >= 4)

        for y in range(0, 6):
            for x in range(0, 8):
                im.set_color(x + 3, y + 1, get_color(x * 4, y * 4))

        return im.bake()

    def destroy(self): ...
    def serialize(self) -> bytes: ...
    def deserialize(self, data: bytes): ...
    def session_join(self, client_id: int) -> bool: ...
    def session_leave(self, client_id: int): ...
    def is_powered_on(self) -> bool: ...
    def is_first_session(self) -> bool: ...
    def set_power(self, on: bool): ...
    def get_memory(self, offset: int, size: int) -> bytes: ...
    def set_memory(self, offset: int, data: bytes): ...
    def get_ula(self) -> int: ...
    def set_key(self, row: int, value: int): ...
    def reboot(self): ...
    def nmi(self): ...
    def get_hostname(self) -> bytes: ...
    def get_hash(self) -> bytes: ...
    def set_hostname(self, hostname: bytes): ...
    def post_message(self, message: bytes): ...
    def bind_port_write(self, address: int, cb: Callable[[int], None]): ...
    def bind_port_read(self, address: int, cb: Callable[[], int]): ...
    def bind_memory_write(self, address: int, size: int, cb: Callable[[int, int], None]): ...
    def bind_memory_read(self, address: int, size: int, cb: Callable[[int], int]): ...
    def mount_path(self, path: bytes, factory: MountedXFSFactory) -> bool: ...
    def load_snapshot(self, snapshot: bytes) -> bool: ...
