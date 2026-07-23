
from . api.block import BlockObject, NothingBlockObject
from . import items

from typing import Any, Dict, Callable, Optional
import traceback


class BlockSpawn(object):
    CREATORS: Dict[items.Item, Callable[[Any], BlockObject]] = {
        items.NOTHING: lambda kwargs: NothingBlockObject(0),
    }

    CREATORS_STR: Dict[bytes, items.Item] = {}

    @staticmethod
    def register(item: items.Item, cb: Callable[[Any], BlockObject]):
        BlockSpawn.CREATORS[item] = cb
        BlockSpawn.CREATORS_STR[item.identity.encode()] = item

    @staticmethod
    def init():
        for k, v in BlockSpawn.CREATORS.items():
            BlockSpawn.CREATORS_STR[k.identity.encode()] = v

    @staticmethod
    def create_block(item: items.Item, **kwargs) -> Optional[BlockObject]:
        if item not in BlockSpawn.CREATORS:
            return None
        try:
            bo = BlockSpawn.CREATORS[item](**kwargs)
        except Exception as e:
            print(traceback.format_exc())
            return None

        bo.set_item(item)
        return bo

    @staticmethod
    def create_block_str(s: bytes, **kwargs) -> Optional[BlockObject]:
        if s not in BlockSpawn.CREATORS_STR:
            return None
        bo_item = BlockSpawn.CREATORS_STR[s]
        return BlockSpawn.create_block(bo_item, **kwargs)


BlockSpawn.init()
create_block_str = BlockSpawn.create_block_str
