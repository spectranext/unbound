from typing import Set, Dict, List, Tuple, Generator, Optional

from ..air import AirProducer
from ..api.client import ClientAPI
from .. api.map import MapAPI
from .. api.block import BlockObject, NeighboringBlockObject
from .. scenarios import get_scenario
from .. import blocks
from .. import items
from .. blockspawn import BlockSpawn
from .. import loc
import time
import math


def generate_sky(server_map: MapAPI):

    sc = get_scenario(server_map.scenario)

    day = server_map.is_day()

    # so it doesn't regenerate too often
    if server_map.day == day:
        return

    server_map.day = day

    s = items.get(".") if day else items.get("^")

    def pass_sky_filter(x_coord, y_coord):
        ajd_angle = x_coord / 4
        compare_to = 20 + (math.cos(ajd_angle) * 2) + (math.cos(ajd_angle + x_coord / 8) * 1)
        return y_coord < compare_to

    for y in range(0, server_map.top_placing_block):
        for x in range(0, server_map.get_width()):
            if not pass_sky_filter(x, y):
                continue
            server_map.set_block(x, y, BlockSpawn.create_block(s), True)


def do_refresh_map(server_map: MapAPI) -> Generator:
    from .. player import PlayerObject
    from .. linkedblocks import LinkedBlockObject
    from .. bases.light import LightBaseInstance

    scenario: bytes = server_map.scenario
    map_width = server_map.get_width()
    map_height = server_map.get_height()

    sc = get_scenario(scenario)

    generate_sky(server_map)

    cnt = 0

    yield

    server_map.print(f"refreshing light")

    if not sc.light:
        for y in range(0, map_height):
            for x in range(0, map_width):
                b = server_map.get_block(x, y)
                if b is None:
                    continue
                b.set_light(blocks.MAXIMUM_LIGHT)
    else:

        light_top = 28

        for y in range(0, map_height):
            for x in range(0, map_width):
                b = server_map.get_block(x, y)
                if b is None:
                    continue
                b.reset()
                cnt += 1
                if cnt >= 100:
                    cnt = 0
                    yield

        initial_light = blocks.MAXIMUM_LIGHT
        horizontal_light_grace = 16
        horizontal_light_loss = 1

        to_fill: Set[Tuple[int, int, int, int]] = set()
        for x in range(1, map_width - 1):
            to_fill.add((x, 0, initial_light, horizontal_light_grace))
        for base in server_map.bases.entries.values():
            if not isinstance(base, LightBaseInstance):
                continue
            base.sync_light_block()
            if not base.has_light():
                continue
            x, y = base.get_light_source()
            if 0 <= x <= map_width - 1 and 0 <= y <= map_height - 1:
                for nx, ny in [
                    (x - 1, y), (x + 1, y), (x - 1, y - 1), (x + 1, y + 1),
                    (x - 1, y + 1), (x + 1, y - 1), (x, y - 1), (x, y + 1)
                ]:
                    if 0 <= nx <= map_width - 1 and 0 <= ny <= map_height - 1:
                        to_fill.add((nx, ny, initial_light, horizontal_light_grace))
        best_light_spread: Dict[Tuple[int, int], List[Tuple[int, int]]] = {}

        while len(to_fill):
            (x, y, light_value, horizontal_spread) = to_fill.pop()
            block = server_map.get_block(x, y)
            if block is None:
                continue

            best = best_light_spread.get((x, y), [])
            if any(best_light >= light_value and best_spread >= horizontal_spread for best_light, best_spread in best):
                continue

            best_light_spread[(x, y)] = [
                (best_light, best_spread)
                for best_light, best_spread in best
                if best_light > light_value or best_spread > horizontal_spread
            ]
            best_light_spread[(x, y)].append((light_value, horizontal_spread))

            if block.get_light() < light_value:
                block.set_light(light_value)

            next_light_value = block.pass_blocking_light(light_value) if block.blocking() else block.pass_light(light_value)

            cnt += 1
            if cnt >= 100:
                cnt = 0
                yield

            if next_light_value < 0:
                continue

            for nx, ny in [
                (x - 1, y), (x + 1, y), (x - 1, y - 1), (x + 1, y + 1),
                (x - 1, y + 1), (x + 1, y - 1), (x, y - 1), (x, y + 1)
            ]:
                if 0 <= nx <= map_width - 1 and 0 <= ny <= map_height - 1:
                    neighbor_light_value = next_light_value
                    neighbor_horizontal_spread = horizontal_spread
                    if nx != x:
                        if neighbor_horizontal_spread > 0:
                            neighbor_horizontal_spread -= 1
                        else:
                            neighbor_light_value -= horizontal_light_loss
                    if neighbor_light_value >= 0:
                        to_fill.add((nx, ny, neighbor_light_value, neighbor_horizontal_spread))

    next_zone_id = 1
    to_fill: Set[Tuple[int, int, int]] = set()
    pressure_zones: Dict[int, List[Tuple[int, int]]] = {}

    for producer in server_map.air.get_all_producers():
        pressure_zones[next_zone_id] = list()
        to_fill.add((producer.get_x(), producer.get_y(), next_zone_id))

    while len(to_fill):
        (x, y, pressure_zone) = to_fill.pop()
        block = server_map.get_block(x, y)

        if block is None:
            continue

        if block.get_pressure_zone():
            bz = block.get_pressure_zone()
            if bz != pressure_zone:
                # it's a different pressure zone - must merge
                zz = pressure_zones[bz]
                for nx, ny in zz:
                    server_map.get_block(nx, ny).set_pressure(pressure_zone)
                del pressure_zones[bz]
                server_map.print(f"Merged pressure zone {bz} into {pressure_zone}")
            continue

        if block.blocking():
            continue

        if pressure_zone not in pressure_zones:
            continue

        z = pressure_zones[pressure_zone]

        # it's a leak
        if len(z) > 4096:
            server_map.print(f"Pressure zone {pressure_zone} is leaking.")
            for xx, yy in z:
                server_map.get_block(xx, yy).reset_pressure()
            del pressure_zones[pressure_zone]
            continue

        z.append((x, y))
        block.set_pressure(pressure_zone)

        cnt += 1
        if cnt >= 100:
            cnt = 0
            yield

        for nx, ny in [
            (x - 1, y), (x + 1, y), (x - 1, y - 1), (x + 1, y + 1),
            (x - 1, y + 1), (x + 1, y - 1), (x, y - 1), (x, y + 1)
        ]:
            if 0 <= nx <= map_width - 1 and 0 <= ny <= map_height - 1:
                to_fill.add((nx, ny, pressure_zone))

    server_map.print(f"Pressure zones:")
    pressure_zone_producers: Dict[int, List[AirProducer]] = {}

    for zone_id, l in pressure_zones.items():
        pressure_zone_producers[zone_id] = list()

    for producer in server_map.air.get_all_producers():
        producer.set_pressure_zone_blocks([])
        b = server_map.get_block(producer.get_x(), producer.get_y())
        if not b.get_pressure_zone():
            continue
        if b.get_pressure_zone() not in pressure_zones:
            continue
        producer.set_pressure_zone_blocks(pressure_zones[b.get_pressure_zone()])
        pressure_zone_producers[b.get_pressure_zone()].append(producer)

    for zone_id, l in pressure_zones.items():
        l1 = len(l)
        p1 = len(pressure_zone_producers[zone_id])
        server_map.print(f" - {zone_id}: {l1} blocks, {p1} producers")

    for y in range(0, map_height):
        for x in range(0, map_width):
            b = server_map.get_block(x, y)
            if not b.get_pressure_zone():
                # reset air for every block that lost pressure
                b.set_air(0)
            server_map.update_block(x, y, True)
            cnt += 1
            if cnt >= 100:
                cnt = 0
                yield


refresh_map_generator: Optional[Generator] = None


def refresh_map(server_map: MapAPI) -> bool:
    global refresh_map_generator
    gen = refresh_map_generator
    if gen:
        try:
            next(refresh_map_generator)
        except StopIteration:
            del refresh_map_generator
            refresh_map_generator = None
            return True
        else:
            return False
    else:
        refresh_map_generator = do_refresh_map(server_map)
        return False
