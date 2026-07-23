from typing import Optional, Tuple, Set, Dict, List, TYPE_CHECKING

from . block import NeighboringBlockObject, NeighboringSet

if TYPE_CHECKING:
    from . map import MapAPI


class NetworkBlockInterface(object):
    def __init__(self, net: 'BlockNetwork'):
        self.network = net
        self.network.register(self)
        self.network.init()
        self.visit: int = -1

    def get_location(self) -> Tuple[int, int]:
        return 0, 0

    def net_class(self) -> Optional[str]:
        return None

    def check_networks(self):
        from . map import MapAPI
        self.network.check_networks(MapAPI.instance)

    def unregister(self):
        self.network.unregister(self)

    def release_net(self):
        self.network = None


class BlockNetwork(object):
    NEXT_ID = 0
    VISIT = 0

    def __init__(self, tag: str, blocks: Set['NetworkBlockInterface']):
        self.tag = tag
        self.network_id = BlockNetwork.NEXT_ID
        BlockNetwork.NEXT_ID += 1
        self.blocks: Set['NetworkBlockInterface'] = blocks
        self.classified: Dict[str, Set['NetworkBlockInterface']] = {}

        from . map import MapAPI
        MapAPI.instance.print("New net {0}.{1}".format(self.tag, str(self.network_id)))

    def init(self):
        self.classify()

    def register(self, b: 'NetworkBlockInterface'):
        self.blocks.add(b)

    def get_classified(self, c: str) -> Optional[Set['NetworkBlockInterface']]:
        return self.classified.get(c, None)

    def unregister(self, b: 'NetworkBlockInterface'):
        self.blocks.remove(b)
        from . map import MapAPI
        if len(self.blocks) == 0:
            MapAPI.instance.print("Net {0}.{1} now empty".format(self.tag, str(self.network_id)))
            return

    def release(self):
        self.blocks = set()
        self.classified.clear()

    def set_blocks(self, b: Set['NetworkBlockInterface']):
        self.blocks = b
        self.classify()

    def classify(self):
        self.classified.clear()
        for b in self.blocks:
            b.network = self
            classified = b.net_class()
            if classified is None:
                continue
            s = self.classified.get(classified, None)
            if s is None:
                s = set()
                self.classified[classified] = s
            s.add(b)

    def check_networks(self, map_api: 'MapAPI'):
        visit = BlockNetwork.VISIT
        BlockNetwork.VISIT += 1
        map_width = map_api.get_width()
        map_height = map_api.get_height()

        groups: List[Set['NetworkBlockInterface']] = []
        current_group: Set['NetworkBlockInterface'] = set()
        # try and split the blocks into groups if possible
        network = self.blocks.copy()
        while len(network):
            # start a wave from a block within the network
            block_check = network.pop()
            to_check: Set['NetworkBlockInterface'] = set()
            to_check.add(block_check)
            while len(to_check):
                # take a block from a wave, remove it from the network and fill it into the current group
                next_block = to_check.pop()
                if next_block in network:
                    network.remove(next_block)
                current_group.add(next_block)

                x, y = next_block.get_location()

                for nx, ny in [(x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)]:
                    if 0 <= nx <= map_width - 1 and 0 <= ny <= map_height - 1:
                        # grow the wave to neighboring blocks
                        b = map_api.get_block(nx, ny)
                        if not isinstance(b, NetworkBlockInterface):
                            continue
                        if b.network is None:
                            continue
                        if b.visit == visit:
                            continue
                        # this block is from different kind of network
                        if b.network.tag != self.tag:
                            continue
                        if b.network != self:
                            # combine network with other network
                            current_group.update(b.network.blocks)
                            map_api.print("Net {0}.{1} combined with net {2} (+{3} b)".format(
                                self.tag, str(self.network_id), str(b.network.network_id), str(len(b.network.blocks))))
                            b.network = self
                            net = b.network
                            for b in net.blocks:
                                # clear the network for now
                                b.network = None
                            net.release()
                            continue
                        b.visit = visit
                        to_check.add(b)

            # wave is over, current group is a complete network
            if len(current_group):
                groups.append(current_group)
                current_group = set()

        if len(groups) == 0:
            return

        # take the first group it'll be us
        first_group = groups.pop()
        self.set_blocks(first_group)

        for b in self.blocks:
            b.network = self

        # other groups are going to become separate networks
        while len(groups):
            next_group = groups.pop()
            new_network = BlockNetwork(self.tag, next_group)
            map_api.print("Net {0}.{1} separated from {2} ({3} b)".format(
                new_network.tag, str(new_network.network_id), str(self.network_id), str(len(next_group))
            ))
            new_network.init()

    def merge(self, n: 'BlockNetwork'):
        self.blocks.update(n.blocks)
        n.blocks = set()


class NetworkBlockObject(NeighboringBlockObject, NetworkBlockInterface):
    def __init__(self, neighboring_set: NeighboringSet, network_tag: str,
                 proto: Dict[bytes, bytes] = None, **kwargs):
        NeighboringBlockObject.__init__(self, neighboring_set, False, proto=proto, **kwargs)
        NetworkBlockInterface.__init__(self, BlockNetwork(network_tag, set()))

    def refresh_neighbors(self, notify: bool):
        super().refresh_neighbors(notify)
        self.check_networks()

    def get_location(self) -> Tuple[int, int]:
        return self.x, self.y

    def release(self, notify: bool):
        super().unregister()
        super().release(notify)
        super().release_net()
