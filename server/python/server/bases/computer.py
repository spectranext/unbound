import functools
from typing import Dict, Optional, TYPE_CHECKING

from . import BaseItem, BaseInstance
from .. api.client import ClientAPI
from .. api.map import MapAPI
from .. api.computer import ComputerAPI
from .. api.query import QueryResponse, OPT
from .power import PowerConsumer
from .. import blocks
from .. api.object import ObjectAPI
from .. import loc

if TYPE_CHECKING:
    from ..team import Team


class ComputerSetHostnameQueryResponse(QueryResponse):
    def __init__(self, bi: 'ComputerBaseInstance', player: ObjectAPI):
        super().__init__(b"", loc.COMPUTER_NEW_HOSTNAME.encode())
        self.bi = bi

        self.player = player
        self.edit = True
        self.actions = [loc.OK.encode()]

    def selected(self, option: int, action: bytes) -> Optional['QueryResponse']:
        self.bi.cpu.set_hostname(action)
        return ComputerQueryResponse(self.bi, self.player)


class ComputerPostMessageQueryResponse(QueryResponse):
    def __init__(self, bi: 'ComputerBaseInstance', player: ObjectAPI):
        super().__init__(b"", loc.COMPUTER_POST_MESSAGE.encode())
        self.bi = bi

        self.player = player
        self.edit = True
        self.actions = [loc.OK.encode()]

    def selected(self, option: int, action: bytes) -> Optional['QueryResponse']:
        message = action.decode().replace(",", "\n")
        self.bi.cpu.post_message("{0}\n".format(message).encode())
        return ComputerQueryResponse(self.bi, self.player)


class ComputerQueryResponse(QueryResponse):
    def __init__(self, bi: 'ComputerBaseInstance', player: ObjectAPI):
        super().__init__(b"", "/{0}/ {1}".format(bi.cpu.get_hostname().decode(), loc.COMPUTER).encode())
        self.bi = bi
        self.player = player
        self.image = self.bi.cpu.get_image()

        self.options = [
            OPT(loc.COMPUTER_TURN_OFF if bi.power_on else loc.COMPUTER_TURN_ON, self.switch_power)
        ]
        if self.bi.power_on:
            self.options.append(OPT(loc.COMPUTER_OPEN, self.open))
            self.options.append(OPT(loc.COMPUTER_REBOOT, self.reboot))
            self.options.append(OPT(loc.COMPUTER_HOSTNAME.format(self.bi.cpu.get_hostname().decode()),
                                    self.set_hostname))
            self.options.append(OPT(loc.COMPUTER_POST_MESSAGE, self.post_message))
            self.options.append(OPT(loc.COMPUTER_SAVE_SNAPSHOT, self.save_snapshot))
            if self.bi.snapshot:
                self.options.append(OPT(loc.COMPUTER_RESTORE_SNAPSHOT, self.restore_snapshot))
            self.options.append(OPT(loc.COMPUTER_NMI, self.nmi))
        self.actions = [loc.OK.encode()]

    def switch_power(self, action: bytes) -> Optional[QueryResponse]:
        self.bi.power_on = not self.bi.power_on
        return None

    @staticmethod
    def drive_screen(self, client: ClientAPI):
        client.push_module(b"")

    def reboot(self, action: bytes) -> Optional[QueryResponse]:
        self.bi.cpu.reboot()
        return self.open(action)

    def nmi(self, action: bytes) -> Optional[QueryResponse]:
        self.bi.cpu.nmi()
        return self.open(action)

    def set_hostname(self, action: bytes) -> Optional[QueryResponse]:
        return ComputerSetHostnameQueryResponse(self.bi, self.player)

    def post_message(self, action: bytes) -> Optional[QueryResponse]:
        return ComputerPostMessageQueryResponse(self.bi, self.player)

    def save_snapshot(self, action: bytes) -> Optional[QueryResponse]:
        self.bi.snapshot = self.bi.cpu.serialize()
        return None

    def restore_snapshot(self, action: bytes) -> Optional[QueryResponse]:
        if not self.bi.snapshot:
            return
        self.bi.cpu.deserialize(self.bi.snapshot)
        return self.open(action)

    def open(self, action: bytes) -> Optional[QueryResponse]:
        from .. player import PlayerObject
        from .. api.map import MapAPI

        if not isinstance(self.player, PlayerObject):
            return None

        c = self.player.client
        if c is None:
            return None

        MapAPI.instance.schedule_callback(
            functools.partial(c.computer_session, self.bi.cpu, None, self.bi.first_boot), 500)
        return None


class ComputerBaseInstance(BaseInstance, PowerConsumer):
    INSTANCES = 1

    def __init__(self, x: int, y: int, prototype: 'BaseItem', team: Optional['Team'], power_consumption: int, **kwargs):
        super().__init__(x, y, prototype, team, **kwargs)
        PowerConsumer.__init__(self)
        self.instance_id = ComputerBaseInstance.INSTANCES
        ComputerBaseInstance.INSTANCES += 1
        self.snapshot = None
        self.cpu: Optional[ComputerAPI] = None
        self.cpu_hash: Optional[bytes] = None
        self.cpu_hostname: Optional[bytes] = None
        self.cpu_data: Optional[bytes] = None

        self.first_boot = True
        self.has_power = False
        self.power_on = True
        self.power_consumption = power_consumption

    def on_init(self):
        self.cpu = MapAPI.instance.computer_new(self.team.team_id if self.team else 0, self.cpu_hash)
        self.cpu_hash = None

        if self.cpu_hostname:
            self.cpu.set_hostname(self.cpu_hostname)
            self.cpu_hostname = None

        if self.cpu_data:
            self.cpu.deserialize(self.cpu_data)
            self.cpu_data = None

    def on_destroy(self):
        super().on_destroy()
        if self.cpu:
            self.cpu.destroy()
            self.cpu = None

    def get_status(self) -> bytes:
        if self.has_power:
            return b"Powered ON"
        else:
            return b"NO POWER"

    def deserialize(self, data: Dict[bytes, bytes]):
        super().deserialize(data)
        if b"cpu" in data:
            self.cpu_data = data[b"cpu"]
        if b"hostname" in data:
            self.cpu_hostname = data[b"hostname"]
        if b"hash" in data:
            self.cpu_hash = data[b"hash"]
        self.power_on = data.get(b"pw", b"yes") == b"yes"
        self.first_boot = False

    def serialize(self) -> Dict[bytes, bytes]:
        d = super().serialize()
        d[b"cpu"] = self.cpu.serialize()
        d[b"hostname"] = self.cpu.get_hostname()
        d[b"hash"] = self.cpu.get_hash()
        d[b"pw"] = b"yes" if self.power_on else b"no"
        return d

    def on_update(self):
        self.has_power = self.get_power_equilibrium() > 0
        b = self.get_block(0, 1)
        code = blocks.COMPUTER_ON if self.has_power else blocks.COMPUTER_OFF

        if b and b.code != code:
            self.set_block_code(0, 1, code)

        run = self.has_power and self.power_on
        if run != self.cpu.is_powered_on():
            self.cpu.set_power(run)

    def get_consumer_power(self) -> int:
        return self.power_consumption

    def consumes_power(self) -> bool:
        return self.power_on

    def get_x(self) -> int:
        return self.x

    def get_y(self) -> int:
        return self.y

    def query_instance(self, player: ObjectAPI) -> Optional['QueryResponse']:
        if player.get_team() != self.team:
            return None
        return ComputerQueryResponse(self, player)


class ComputerBaseItem(BaseItem):
    def __init__(self, identity: str):
        super().__init__(identity)
        self.power_consumption = 10

    def get_instance(self, x: int, y: int, team: Optional['Team'], **kwargs) -> BaseInstance:
        return ComputerBaseInstance(x, y, self, team, self.power_consumption, **kwargs)

    def parse(self, v):
        super().parse(v)
        if "power_consumption" in v:
            self.power_consumption = v["power_consumption"]
