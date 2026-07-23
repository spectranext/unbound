from typing import Optional

from .. api.object import ObjectAPI


class BotPlayerObject(ObjectAPI):
    def __init__(self, object_type: int, data_entry: bytes, move_entry: bytes, picking_entry: Optional[bytes]):
        ObjectAPI.__init__(self, object_type, data_entry, move_entry, picking_entry)

    def get_flash_time(self):
        return 0.5
