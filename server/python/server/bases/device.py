from typing import Optional, Dict
from .. api.query import QueryResponse, OPT
from .. api.device import DeviceAPIHandler
from .. api.object import ObjectAPI
from .. import loc

import abc


class Device(object, metaclass=abc.ABCMeta):
    def __init__(self, namespace_id: int, prefix: bytes, listen: Optional[DeviceAPIHandler] = None):
        super().__init__()
        from .. api.map import MapAPI
        self.device = MapAPI.instance.device_new(namespace_id, prefix)
        if self.device and listen:
            self.device.listen(listen)

    def dev_serialize(self, data: Dict[bytes, bytes]):
        data[b"hostname"] = self.device.get_hostname()

    def dev_deserialize(self, data: Dict[bytes, bytes]):
        if b"hostname" in data:
            self.device.set_hostname(data[b"hostname"])

    def device_destroy(self):
        self.device.destroy()
        self.device = None


class DeviceSetHostnameQueryResponse(QueryResponse):
    def __init__(self, bi: Device, player: ObjectAPI):
        super().__init__(b"", loc.COMPUTER_NEW_HOSTNAME.encode())
        self.bi = bi
        self.player = player
        self.edit = True
        self.actions = [loc.OK.encode()]

    def selected(self, option: int, action: bytes) -> Optional['QueryResponse']:
        self.bi.device.set_hostname(action)
        return DeviceQueryResponse(self.bi, self.player)


class DevicePostMessageQueryResponse(QueryResponse):
    def __init__(self, bi: Device, player: ObjectAPI):
        super().__init__(b"", loc.COMPUTER_POST_MESSAGE.encode())
        self.bi = bi

        self.player = player
        self.edit = True
        self.actions = [loc.OK.encode()]

    def selected(self, option: int, action: bytes) -> Optional['QueryResponse']:
        self.bi.device.post_message("{0}\n".format(action.decode()).encode())
        return None


class DeviceQueryResponseOptions(object):
    @staticmethod
    def yield_options(dev: Device, player: ObjectAPI):
        return [
            OPT(loc.COMPUTER_HOSTNAME.format(dev.device.get_hostname().decode()),
                lambda action: DeviceSetHostnameQueryResponse(dev, player)),
            OPT(loc.COMPUTER_POST_MESSAGE,
                lambda action: DevicePostMessageQueryResponse(dev, player))
        ]


class DeviceQueryResponse(QueryResponse):
    def __init__(self, dev: Device, player: ObjectAPI):
        super().__init__(b"", dev.device.get_hostname())
        self.dev = dev
        self.player = player

        self.options = DeviceQueryResponseOptions.yield_options(dev, player)
        self.actions = [loc.OK.encode()]
