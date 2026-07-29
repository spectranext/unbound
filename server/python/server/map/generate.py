import time
from typing import Dict, List, Optional, Set, Tuple

import random
import platypus
import math

from .. api.map import MapAPI
from .. api.block import NeighboringBlockObject
from .. api.block import BlockObject
from .. blockspawn import BlockSpawn
from .. scenarios import get_scenario
from .. import items
from .. import blocks
from .. items.generation import Generation
from .. bases import spawn_base, BaseItem
from . refresh import refresh_map, generate_sky
from . level import lower_than_ground_line
from .. team import TEAMS


CAVE_SURFACE_BUFFER = 5
CAVE_EDGE_MARGIN = 3
CAVE_STEP_MIN = 12
CAVE_STEP_MAX = 18
CAVE_ROOM_RADIUS_X = (3, 5)
CAVE_ROOM_RADIUS_Y = (2, 3)
CAVE_BRANCH_CHANCE = 0.35
CAVE_BRANCH_RADIUS_X = (2, 3)
CAVE_BRANCH_RADIUS_Y = (1, 2)
CAVE_MAX_CENTER_STEP_Y = 4
CAVE_TUNNEL_RADIUS_X = 1
CAVE_TUNNEL_RADIUS_Y = 1
CAVE_SIDE_POCKET_CHANCE = 0.25
CAVE_STRAND_LINK_CHANCE = 0.15
CAVE_STRAND_LINK_SPACING = 7
CAVE_STRAND_DEPTHS = (
    (8, 15),
    (18, 27),
    (29, 42),
)
CAVE_PRESERVED_MINERALS = {
    "coal",
    "ore_iron",
    "ore_copper",
    "ore_gold",
    "spectrum",
}
COAL_SEAMS_PER_128_BLOCKS = 6
COAL_SEAM_SIZE = (18, 38)
COAL_SEAM_RADIUS = 3
COAL_MIN_CLUSTER_SIZE = 12
ORE_SEAMS: Dict[str, Dict[str, object]] = {
    "ore_iron": {
        "seams_per_128_blocks": 2,
        "size": COAL_SEAM_SIZE,
        "radius": 3,
        "min_cluster_size": 12,
    },
    "ore_copper": {
        "seams_per_128_blocks": 2,
        "size": (10, 22),
        "radius": 2,
        "min_cluster_size": 7,
    },
    "ore_gold": {
        "seams_per_128_blocks": 1,
        "size": (4, 10),
        "radius": 2,
        "min_cluster_size": 3,
    },
}


def generate_edge_blockers(server_map: MapAPI):
    map_width = server_map.get_width()
    if map_width <= 0:
        return

    edge_xs = (0,) if map_width == 1 else (0, map_width - 1)
    for y in range(0, server_map.get_height()):
        for x in edge_xs:
            server_map.set_block(x, y, BlockObject(blocks.EMPTY_BUT_BLOCKING, items.NOTHING), False)


def get_to_heights(server_map: MapAPI):
    def _top(x: int) -> int:
        for y2 in range(0, server_map.get_height()):
            b2 = server_map.get_block(x, y2)
            if b2 and b2.blocking():
                return y2
        return 0

    return [
        _top(x)
        for x in range(0, server_map.get_width())
    ]


def _surface_y(x: int, line: float, angle: float, sc) -> float:
    ajd_angle = angle + x / 16
    return line + (math.cos(ajd_angle) * sc.map_wave_a) + \
        (math.cos(ajd_angle + x / 24) * sc.map_wave_b)


def _is_cave_depth(x: int, y: int, map_width: int, map_height: int, angle: float, sc) -> bool:
    if x < CAVE_EDGE_MARGIN or x >= map_width - CAVE_EDGE_MARGIN:
        return False
    if y < 0 or y >= map_height - 2:
        return False
    return y >= _surface_y(x, Generation.MIDDLE_LINE, angle, sc) + CAVE_SURFACE_BUFFER


def _carve_ellipse(
        cave_cells: Set[Tuple[int, int]],
        map_width: int,
        map_height: int,
        angle: float,
        sc,
        cx: int,
        cy: int,
        radius_x: int,
        radius_y: int):
    for y in range(cy - radius_y, cy + radius_y + 1):
        for x in range(cx - radius_x, cx + radius_x + 1):
            if not _is_cave_depth(x, y, map_width, map_height, angle, sc):
                continue
            dx = (x - cx) / max(1, radius_x)
            dy = (y - cy) / max(1, radius_y)
            if dx * dx + dy * dy <= 1.0:
                cave_cells.add((x, y))


def _carve_tunnel(
        cave_cells: Set[Tuple[int, int]],
        map_width: int,
        map_height: int,
        angle: float,
        sc,
        start: Tuple[int, int],
        end: Tuple[int, int]):
    x1, y1 = start
    x2, y2 = end
    distance = max(abs(x2 - x1), abs(y2 - y1), 1)

    for step in range(0, distance + 1):
        t = step / distance
        x = int(round(x1 + (x2 - x1) * t))
        y = int(round(y1 + (y2 - y1) * t))
        _carve_ellipse(cave_cells, map_width, map_height, angle, sc, x, y,
                       CAVE_TUNNEL_RADIUS_X, CAVE_TUNNEL_RADIUS_Y)


def _clamp_cave_y(map_height: int, surface: float, y: int) -> int:
    return max(int(surface) + CAVE_SURFACE_BUFFER + 3, min(map_height - 6, y))


def _carve_side_pocket(
        cave_cells: Set[Tuple[int, int]],
        map_width: int,
        map_height: int,
        angle: float,
        sc,
        center: Tuple[int, int]):
    x, y = center
    pocket_x = max(
        CAVE_EDGE_MARGIN,
        min(map_width - CAVE_EDGE_MARGIN - 1, x + random.choice((-1, 1)) * random.randint(4, 9)))
    pocket_surface = _surface_y(pocket_x, Generation.MIDDLE_LINE, angle, sc)
    pocket_y = _clamp_cave_y(map_height, pocket_surface, y + random.randint(-4, 5))
    pocket = (pocket_x, pocket_y)
    _carve_tunnel(cave_cells, map_width, map_height, angle, sc, center, pocket)
    _carve_ellipse(
        cave_cells,
        map_width,
        map_height,
        angle,
        sc,
        pocket_x,
        pocket_y,
        random.randint(CAVE_BRANCH_RADIUS_X[0], CAVE_BRANCH_RADIUS_X[1]),
        random.randint(CAVE_BRANCH_RADIUS_Y[0], CAVE_BRANCH_RADIUS_Y[1]))


def _generate_cave_strand(
        cave_cells: Set[Tuple[int, int]],
        map_width: int,
        map_height: int,
        angle: float,
        sc,
        depth_from: int,
        depth_to: int) -> List[Tuple[int, int]]:
    centers: List[Tuple[int, int]] = []

    x = CAVE_EDGE_MARGIN + random.randint(2, 6)
    previous_y: Optional[int] = None

    while x < map_width - CAVE_EDGE_MARGIN:
        surface = _surface_y(x, Generation.MIDDLE_LINE, angle, sc)
        natural_y = int(surface + random.randint(depth_from, depth_to))

        if previous_y is None:
            y = natural_y
        else:
            low = previous_y - CAVE_MAX_CENTER_STEP_Y
            high = previous_y + CAVE_MAX_CENTER_STEP_Y
            y = max(low, min(high, natural_y))

        y = _clamp_cave_y(map_height, surface, y)
        center = (x, y)
        centers.append(center)

        radius_x = random.randint(CAVE_ROOM_RADIUS_X[0], CAVE_ROOM_RADIUS_X[1])
        radius_y = random.randint(CAVE_ROOM_RADIUS_Y[0], CAVE_ROOM_RADIUS_Y[1])
        _carve_ellipse(cave_cells, map_width, map_height, angle, sc, x, y, radius_x, radius_y)

        if len(centers) > 1:
            _carve_tunnel(cave_cells, map_width, map_height, angle, sc, centers[-2], center)

        if random.random() < CAVE_BRANCH_CHANCE:
            _carve_side_pocket(cave_cells, map_width, map_height, angle, sc, center)
        if random.random() < CAVE_SIDE_POCKET_CHANCE:
            _carve_side_pocket(cave_cells, map_width, map_height, angle, sc, center)

        previous_y = y
        x += random.randint(CAVE_STEP_MIN, CAVE_STEP_MAX)

    return centers


def _nearest_center(x: int, centers: List[Tuple[int, int]]) -> Optional[Tuple[int, int]]:
    if not centers:
        return None
    return min(centers, key=lambda center: abs(center[0] - x))


def _generate_cave_cells(map_width: int, map_height: int, angle: float, sc) -> Set[Tuple[int, int]]:
    cave_cells: Set[Tuple[int, int]] = set()
    strands: List[List[Tuple[int, int]]] = []

    for depth_from, depth_to in CAVE_STRAND_DEPTHS:
        if Generation.MIDDLE_LINE + depth_from >= map_height - 4:
            continue
        strands.append(_generate_cave_strand(
            cave_cells, map_width, map_height, angle, sc, depth_from, depth_to))

    for strand_index in range(1, len(strands)):
        previous = strands[strand_index - 1]
        current = strands[strand_index]
        for center_index, center in enumerate(current):
            if center_index % CAVE_STRAND_LINK_SPACING != strand_index % CAVE_STRAND_LINK_SPACING:
                continue
            if random.random() >= CAVE_STRAND_LINK_CHANCE:
                continue
            closest = _nearest_center(center[0], previous)
            if closest is not None:
                _carve_tunnel(cave_cells, map_width, map_height, angle, sc, closest, center)

    return cave_cells


def _is_preserved_mineral(block) -> bool:
    return bool(block and block.item and block.item.identity in CAVE_PRESERVED_MINERALS)


def _is_item(block, identity: str) -> bool:
    return bool(block and block.item and block.item.identity == identity)


def _find_cave_wall_for_mineral(
        server_map: MapAPI,
        cave_cells: Set[Tuple[int, int]],
        x: int,
        y: int) -> Optional[Tuple[int, int]]:
    candidates: List[Tuple[int, int]] = []
    fallback_candidates: List[Tuple[int, int]] = []
    map_width = server_map.get_width()
    map_height = server_map.get_height()

    for radius in range(1, 5):
        for yy in range(y - radius, y + radius + 1):
            for xx in range(x - radius, x + radius + 1):
                if xx < 0 or yy < 0 or xx >= map_width or yy >= map_height:
                    continue
                if (xx, yy) in cave_cells:
                    continue
                if abs(xx - x) != radius and abs(yy - y) != radius:
                    continue
                wall = server_map.get_block(xx, yy)
                if not wall or not wall.blocking():
                    continue
                touches_cave = False
                for nx, ny in ((xx + 1, yy), (xx - 1, yy), (xx, yy + 1), (xx, yy - 1)):
                    if (nx, ny) in cave_cells:
                        touches_cave = True
                        break
                if touches_cave:
                    if _is_preserved_mineral(wall):
                        fallback_candidates.append((xx, yy))
                    else:
                        candidates.append((xx, yy))
        if candidates:
            return random.choice(candidates)
        if fallback_candidates:
            return random.choice(fallback_candidates)

    return None


def _is_ore_depth(x: int, y: int, angle: float, sc) -> bool:
    return lower_than_ground_line(x, y, 38, angle, sc) and \
        not lower_than_ground_line(x, y, 64, angle, sc)


def _is_coal_depth(x: int, y: int, angle: float, sc) -> bool:
    return lower_than_ground_line(x, y, 68, angle, sc)


def _cave_wall_candidates(
        server_map: MapAPI,
        cave_cells: Set[Tuple[int, int]],
        depth_check,
        angle: float,
        sc) -> List[Tuple[int, int]]:
    candidates: List[Tuple[int, int]] = []
    map_width = server_map.get_width()
    map_height = server_map.get_height()

    for x, y in cave_cells:
        for xx, yy in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if xx < 0 or yy < 0 or xx >= map_width or yy >= map_height:
                continue
            if (xx, yy) in cave_cells or not depth_check(xx, yy, angle, sc):
                continue
            wall = server_map.get_block(xx, yy)
            if wall and wall.blocking() and not _is_preserved_mineral(wall):
                candidates.append((xx, yy))

    return candidates


def _grow_mineral_seam(
        server_map: MapAPI,
        cave_cells: Set[Tuple[int, int]],
        seed: Tuple[int, int],
        angle: float,
        sc,
        identity: str,
        size: Tuple[int, int],
        radius: int,
        depth_check,
        commit_min_size: int = 1) -> int:
    item = items.get(identity)
    target_size = random.randint(size[0], size[1])
    seam_cells: Set[Tuple[int, int]] = set()
    frontier = [seed]
    map_width = server_map.get_width()
    map_height = server_map.get_height()

    while frontier and len(seam_cells) < target_size:
        index = random.randrange(0, len(frontier))
        x, y = frontier.pop(index)
        if (x, y) in seam_cells or (x, y) in cave_cells:
            continue
        if x < 0 or y < 0 or x >= map_width or y >= map_height:
            continue
        if not depth_check(x, y, angle, sc):
            continue
        if abs(x - seed[0]) > radius * 3 or abs(y - seed[1]) > radius * 2:
            continue
        block = server_map.get_block(x, y)
        if not block or not block.blocking():
            continue
        if _is_preserved_mineral(block) and not _is_item(block, identity):
            continue

        seam_cells.add((x, y))

        neighbors = [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
        if random.random() < 0.55:
            neighbors.append((x + random.choice((-1, 1)), y + random.choice((-1, 1))))
        random.shuffle(neighbors)
        frontier.extend(neighbors)

    if len(seam_cells) < commit_min_size:
        return 0

    for x, y in seam_cells:
        server_map.set_block(x, y, BlockSpawn.create_block(item), False)

    return len(seam_cells)


def _grow_coal_seam(
        server_map: MapAPI,
        cave_cells: Set[Tuple[int, int]],
        seed: Tuple[int, int],
        angle: float,
        sc) -> int:
    return _grow_mineral_seam(
        server_map, cave_cells, seed, angle, sc, "coal", COAL_SEAM_SIZE, COAL_SEAM_RADIUS, _is_coal_depth)


def _enrich_cave_walls_with_coal(server_map: MapAPI, cave_cells: Set[Tuple[int, int]], angle: float, sc) -> int:
    candidates = _cave_wall_candidates(server_map, cave_cells, _is_coal_depth, angle, sc)
    random.shuffle(candidates)

    seams_needed = max(1, (server_map.get_width() * COAL_SEAMS_PER_128_BLOCKS) // 128)
    seams = 0
    placed = 0

    for seed in candidates:
        if seams >= seams_needed:
            break
        placed_in_seam = _grow_coal_seam(server_map, cave_cells, seed, angle, sc)
        if placed_in_seam >= COAL_SEAM_SIZE[0] // 2:
            seams += 1
            placed += placed_in_seam

    return placed


def _item_components(server_map: MapAPI, identity: str) -> List[List[Tuple[int, int]]]:
    item_cells: Set[Tuple[int, int]] = set()

    for y in range(0, server_map.get_height()):
        for x in range(0, server_map.get_width()):
            if _is_item(server_map.get_block(x, y), identity):
                item_cells.add((x, y))

    components: List[List[Tuple[int, int]]] = []
    seen: Set[Tuple[int, int]] = set()

    for cell in item_cells:
        if cell in seen:
            continue
        stack = [cell]
        seen.add(cell)
        component: List[Tuple[int, int]] = []
        while stack:
            x, y = stack.pop()
            component.append((x, y))
            for neighbor in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                if neighbor in item_cells and neighbor not in seen:
                    seen.add(neighbor)
                    stack.append(neighbor)
        components.append(component)

    return components


def _coal_components(server_map: MapAPI) -> List[List[Tuple[int, int]]]:
    return _item_components(server_map, "coal")


def _thicken_small_coal_pockets(server_map: MapAPI, cave_cells: Set[Tuple[int, int]], angle: float, sc) -> int:
    added = 0

    for component in _coal_components(server_map):
        if len(component) >= COAL_MIN_CLUSTER_SIZE:
            continue
        before = len(component)
        after = _grow_coal_seam(server_map, cave_cells, random.choice(component), angle, sc)
        added += max(0, after - before)

    return added


def _remove_small_coal_pockets(server_map: MapAPI) -> int:
    base_block = items.get_item(Generation.BASE_BLOCK)
    removed = 0

    for component in _coal_components(server_map):
        if len(component) >= COAL_MIN_CLUSTER_SIZE:
            continue
        for x, y in component:
            server_map.set_block(x, y, BlockSpawn.create_block(base_block), False)
            removed += 1

    return removed


def _enrich_cave_walls_with_ore(
        server_map: MapAPI,
        cave_cells: Set[Tuple[int, int]],
        angle: float,
        sc,
        identity: str,
        config: Dict[str, object]) -> int:
    candidates = _cave_wall_candidates(server_map, cave_cells, _is_ore_depth, angle, sc)
    random.shuffle(candidates)
    seams_needed = max(1, (server_map.get_width() * int(config["seams_per_128_blocks"])) // 128)
    seams = 0
    placed = 0

    for seed in candidates:
        if seams >= seams_needed:
            break
        placed_in_seam = _grow_mineral_seam(
            server_map,
            cave_cells,
            seed,
            angle,
            sc,
            identity,
            config["size"],
            int(config["radius"]),
            _is_ore_depth,
            int(config["min_cluster_size"]))
        if placed_in_seam >= int(config["min_cluster_size"]):
            seams += 1
            placed += placed_in_seam

    return placed


def _remove_small_ore_pockets(server_map: MapAPI, identity: str, min_cluster_size: int) -> int:
    base_block = items.get_item(Generation.BASE_BLOCK)
    removed = 0

    for component in _item_components(server_map, identity):
        if len(component) >= min_cluster_size:
            continue
        for x, y in component:
            server_map.set_block(x, y, BlockSpawn.create_block(base_block), False)
            removed += 1

    return removed


def _seed_required_ore_pocket(
        server_map: MapAPI,
        cave_cells: Set[Tuple[int, int]],
        angle: float,
        sc,
        identity: str,
        config: Dict[str, object]) -> int:
    if _item_components(server_map, identity):
        return 0

    candidates = _cave_wall_candidates(server_map, cave_cells, _is_ore_depth, angle, sc)
    random.shuffle(candidates)

    for seed in candidates:
        placed = _grow_mineral_seam(
            server_map,
            cave_cells,
            seed,
            angle,
            sc,
            identity,
            config["size"],
            int(config["radius"]),
            _is_ore_depth,
            int(config["min_cluster_size"]))
        if placed >= int(config["min_cluster_size"]):
            return placed

    return 0


def _enrich_cave_walls_with_ores(server_map: MapAPI, cave_cells: Set[Tuple[int, int]], angle: float, sc) -> Tuple[int, int]:
    seeded = 0
    removed = 0

    for identity, config in ORE_SEAMS.items():
        seeded += _enrich_cave_walls_with_ore(server_map, cave_cells, angle, sc, identity, config)
        removed += _remove_small_ore_pockets(server_map, identity, int(config["min_cluster_size"]))

    seeded += _seed_required_ore_pocket(server_map, cave_cells, angle, sc, "ore_gold", ORE_SEAMS["ore_gold"])
    for identity, config in ORE_SEAMS.items():
        removed += _remove_small_ore_pockets(server_map, identity, int(config["min_cluster_size"]))

    return seeded, removed


def _remove_small_resource_pockets(server_map: MapAPI) -> Tuple[int, int]:
    ore_removed = 0

    for identity, config in ORE_SEAMS.items():
        ore_removed += _remove_small_ore_pockets(server_map, identity, int(config["min_cluster_size"]))

    coal_removed = _remove_small_coal_pockets(server_map)
    return ore_removed, coal_removed


def generate_caves(server_map: MapAPI, angle: float, sc):
    cave_cells = _generate_cave_cells(server_map.get_width(), server_map.get_height(), angle, sc)
    preserved = 0

    for x, y in cave_cells:
        block = server_map.get_block(x, y)
        if _is_preserved_mineral(block):
            wall = _find_cave_wall_for_mineral(server_map, cave_cells, x, y)
            if wall is not None:
                server_map.set_block(wall[0], wall[1], BlockSpawn.create_block(block.item), False)
                preserved += 1
        server_map.set_block(x, y, BlockSpawn.create_block(items.NOTHING), False)

    ore_seeded, ore_removed = _enrich_cave_walls_with_ores(server_map, cave_cells, angle, sc)
    coal = _enrich_cave_walls_with_coal(server_map, cave_cells, angle, sc)
    coal += _thicken_small_coal_pockets(server_map, cave_cells, angle, sc)
    removed_coal = _remove_small_coal_pockets(server_map)
    for identity, config in ORE_SEAMS.items():
        ore_removed += _remove_small_ore_pockets(server_map, identity, int(config["min_cluster_size"]))

    server_map.print(
        "carved {0} cave air pockets, preserved {1} minerals, seeded {2} ores, removed {3} ore crumbs, seeded {4} coal, removed {5} coal crumbs".format(
            len(cave_cells), preserved, ore_seeded, ore_removed, coal, removed_coal))


def generate_map(server_map: MapAPI, scenario: bytes):
    server_map.scenario = scenario
    server_map.ensure_day_cycle_anchor(start_in_daytime=True)
    server_map.print("new scenario: {0}".format(scenario.decode()))

    random.seed(None)

    sc = get_scenario(scenario)
    map_width = server_map.get_width()
    map_height = server_map.get_height()
    server_map.print("generating a new map {0}x{1}...".format(map_width, map_height))

    base_block = items.get_item(Generation.BASE_BLOCK)

    for t in TEAMS:
        inventory = t.inventory
        inventory.add_item(items.get("oxygen"), 4, 1.)
        inventory.add_item(items.get("ac"), 8, 1.)
        inventory.add_item(items.get("heat_pack"), 4, 1.)

    angle = random.random()
    server_map.map_generation_angle = angle

    MapAPI.instance.top_placing_block = 24

    for y in range(0, map_height):
        for x in range(0, map_width):
            if lower_than_ground_line(x, y, Generation.MIDDLE_LINE, angle, sc):
                server_map.set_block(x, y, BlockSpawn.create_block(base_block), False)

    for layer in Generation.LAYERS:
        next_id = 1

        weight_ids = {
            0: layer.skip
        }

        id_to_item = {
            0: None
        }

        for k, v in layer.weights.items():
            weight_ids[next_id] = v
            id_to_item[next_id] = k
            next_id += 1

        rough_terrain = platypus.generate(map_width, map_height, layer.smoothness, weight_ids)

        for y, ys in enumerate(rough_terrain):
            for x, value in enumerate(ys):
                item = id_to_item[value]
                if item is None:
                    continue
                if layer.high_limit:
                    if not lower_than_ground_line(x, y, layer.high_limit, angle, sc):
                        continue
                if layer.low_limit:
                    if lower_than_ground_line(x, y, layer.low_limit, angle, sc):
                        continue
                server_map.set_block(x, y, BlockSpawn.create_block(item), False)

    generate_caves(server_map, angle, sc)

    generate_sky(server_map)

    for y in range(0, map_height):
        for x in range(0, map_width):
            if server_map.get_block(x, y) is None:
                server_map.set_block(x, y, BlockSpawn.create_block(items.NOTHING), False)

    if sc.trees:
        generate_objects(server_map)

    generate_edge_blockers(server_map)

    final_ore_removed, final_coal_removed = _remove_small_resource_pockets(server_map)
    if final_ore_removed or final_coal_removed:
        server_map.print("removed {0} final ore crumbs and {1} final coal crumbs".format(
            final_ore_removed, final_coal_removed))


def generate_objects(server_map: MapAPI):

    from . superstructure import Superstructure

    refresh_map(server_map)
    top_heights = get_to_heights(server_map)

    map_width = server_map.get_width()

    # noinspection PyTypeChecker
    fob: BaseItem = items.get("fob")

    for team in TEAMS:
        x = int(map_width * team.location)
        x = max(1, min(map_width - fob.width - 1, x))
        y = top_heights[x]
        for x1 in range(max(1, x - 2), min(map_width - 1, x + fob.width + 2)):
            y1 = top_heights[x1]
            if y1 <= y:
                for y2 in range(y1, y + 1):
                    server_map.set_block(x1, y2, BlockSpawn.create_block(items.NOTHING), False)

        bi = spawn_base(fob, x, y, team)
        bi.tag = server_map.allocate_tag()
        bi.on_init()


    alive_wood = items.get("awd")

    liana = items.get("liana")

    need_trees = map_width // 20
    need_small_trees = map_width // 30
    need_lianas = map_width // 10
    need_flowers = map_width // 16
    need_cobwebs = map_width // 2
    need_rocks = map_width // 12
    need_spikes = map_width // 64
    need_popstones = map_width // 20
    need_spidernests = max(1, map_width // 20)

    flower = items.get("flower")
    spike = items.get("spike")
    popstone = items.get("popstone")
    spidernest_spawner = items.get("spidernest_s")
    spidernest: BaseItem = spidernest_spawner.base

    for g in range(0, need_spikes):
        x = random.randint(0, map_width - 1)

        y = top_heights[x]
        if not isinstance(server_map.get_block(x, y), NeighboringBlockObject):
            continue
        above = server_map.get_block(x, y - 1)
        if above and above.code:
            continue

        server_map.set_block(x, y - 1, BlockSpawn.create_block(spike), False)

    popstones_placed = 0
    spidernests_placed = 0
    if map_width >= 4 and server_map.get_height() > CAVE_SURFACE_BUFFER + 4:
        hazard_floor_candidates: Set[Tuple[int, int]] = set()

        def is_open_block(x_: int, y_: int) -> bool:
            block = server_map.get_block(x_, y_)
            return bool(block is not None and not block.blocking())

        def has_open_hazard_space(x_: int, floor_y_: int, clearance_height: int = 3) -> bool:
            if x_ < 2 or x_ + 2 >= map_width - 1:
                return False
            if floor_y_ < clearance_height or floor_y_ >= server_map.get_height():
                return False

            floor_left = server_map.get_block(x_, floor_y_)
            floor_right = server_map.get_block(x_ + 1, floor_y_)
            if not floor_left or not floor_left.blocking():
                return False
            if not floor_right or not floor_right.blocking():
                return False

            for yy in range(floor_y_ - clearance_height, floor_y_):
                for xx in range(x_, x_ + 2):
                    if not is_open_block(xx, yy):
                        return False

            for yy in range(floor_y_ - min(3, clearance_height), floor_y_):
                if not is_open_block(x_ - 1, yy) or not is_open_block(x_ + 2, yy):
                    return False
            return True

        def has_open_chamber_space(
                x_: int,
                floor_y_: int,
                clearance_height: int = 6,
                min_open_cells: int = 18,
                min_horizontal_run: int = 5,
                min_horizontal_rows: int = 2) -> bool:
            min_x = max(1, x_ - 3)
            max_x = min(map_width - 2, x_ + 4)
            min_y = max(0, floor_y_ - clearance_height)
            max_y = floor_y_ - 1
            open_cells: Set[Tuple[int, int]] = set()

            for yy in range(min_y, max_y + 1):
                for xx in range(min_x, max_x + 1):
                    if is_open_block(xx, yy):
                        open_cells.add((xx, yy))

            seeds = [
                (xx, yy)
                for xx in range(x_, x_ + 2)
                for yy in range(min_y, max_y + 1)
                if (xx, yy) in open_cells
            ]
            if not seeds:
                return False

            connected: Set[Tuple[int, int]] = set()
            stack = [seeds[0]]
            connected.add(seeds[0])
            while stack:
                cx, cy = stack.pop()
                for neighbor in ((cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1)):
                    if neighbor in open_cells and neighbor not in connected:
                        connected.add(neighbor)
                        stack.append(neighbor)

            if len(connected) < min_open_cells:
                return False

            horizontal_rows = 0
            for yy in range(min_y, max_y + 1):
                run = 0
                for xx in range(min_x, max_x + 1):
                    if (xx, yy) in connected:
                        run += 1
                        if run >= min_horizontal_run:
                            horizontal_rows += 1
                            break
                    else:
                        run = 0

            return horizontal_rows >= min_horizontal_rows

        def has_open_spidernest_space(x_: int, y_: int) -> bool:
            floor_y_ = y_ + 1
            if not has_open_hazard_space(x_, floor_y_, clearance_height=6):
                return False
            if not has_open_chamber_space(x_, floor_y_):
                return False

            for yy in range(y_ - spidernest.height + 1, y_ + 1):
                for xx in range(x_, x_ + spidernest.width):
                    if not is_open_block(xx, yy):
                        return False

            for xx in range(x_ - 1, x_ + spidernest.width + 1):
                if not is_open_block(xx, y_ - spidernest.height):
                    return False

            for yy in range(y_ - spidernest.height, y_ + 1):
                if is_open_block(x_ - 1, yy) or is_open_block(x_ + spidernest.width, yy):
                    return True

            return False

        def project_open_space_to_floor(x_: int, open_y: int) -> Optional[int]:
            y_ = open_y
            while y_ < server_map.get_height():
                left = server_map.get_block(x_, y_)
                right = server_map.get_block(x_ + 1, y_)
                if left and right and left.blocking() and right.blocking():
                    return y_ if has_open_hazard_space(x_, y_) else None
                if not is_open_block(x_, y_) or not is_open_block(x_ + 1, y_):
                    return None
                y_ += 1
            return None

        for x in range(2, map_width - 3):
            min_y = max(top_heights[x], top_heights[x + 1]) + CAVE_SURFACE_BUFFER
            for y in range(max(0, min_y), server_map.get_height() - 1):
                if not is_open_block(x, y) or not is_open_block(x + 1, y):
                    continue
                floor_y = project_open_space_to_floor(x, y)
                if floor_y is not None:
                    hazard_floor_candidates.add((x, floor_y))

        popstone_candidates = list(hazard_floor_candidates)
        spidernest_candidates = [(x, floor_y - 1) for x, floor_y in hazard_floor_candidates]
        random.shuffle(popstone_candidates)
        random.shuffle(spidernest_candidates)
        occupied_hazard_cells: Set[Tuple[int, int]] = set()

        for x, y in spidernest_candidates:
            if spidernests_placed >= need_spidernests:
                break
            if not has_open_spidernest_space(x, y):
                continue
            if any((xx, yy) in occupied_hazard_cells for yy in range(y - 1, y + 1) for xx in range(x, x + 2)):
                continue
            if any((xx, yy) in occupied_hazard_cells for yy in range(y - 3, y + 2) for xx in range(x - 1, x + 3)):
                continue

            bi = spawn_base(spidernest, x, y, None)
            bi.tag = server_map.allocate_tag()
            bi.on_init()
            for yy in range(y - 4, y + 1):
                for xx in range(x - 2, x + 4):
                    occupied_hazard_cells.add((xx, yy))
            spidernests_placed += 1

        for x, y in popstone_candidates:
            if popstones_placed >= need_popstones:
                break
            if not has_open_hazard_space(x, y):
                continue
            if any((xx, yy) in occupied_hazard_cells for yy in range(y - 2, y) for xx in range(x, x + 2)):
                continue

            link_id = MapAPI.instance.allocate_link()
            if Superstructure.SUPERSTRUCTURES["popstone"].apply(
                    server_map, x, y, True, True, link=link_id, item=popstone):
                for yy in range(y - 2, y):
                    for xx in range(x, x + 2):
                        occupied_hazard_cells.add((xx, yy))
                popstones_placed += 1

    server_map.print("placed {0}/{1} popstones".format(popstones_placed, need_popstones))
    server_map.print("placed {0}/{1} spider nests".format(spidernests_placed, need_spidernests))

    for g in range(0, need_small_trees):
        x = random.randint(0, map_width - 1)

        y = top_heights[x]
        if not isinstance(server_map.get_block(x, y), NeighboringBlockObject):
            continue

        link_id = MapAPI.instance.allocate_link()

        Superstructure.SUPERSTRUCTURES["tree_small"].apply(
            server_map, x, y, True, True, link=link_id, item=alive_wood)

    for g in range(0, need_trees):
        x = random.randint(0, map_width - 4)

        y = top_heights[x]
        y1 = top_heights[x + 1]
        y2 = top_heights[x + 2]
        if y1 != y:
            continue
        if y2 != y:
            continue
        link_id = MapAPI.instance.allocate_link()

        kinds = ["tree1", "tree2", "tree3"]

        Superstructure.SUPERSTRUCTURES[random.choice(kinds)].apply(
            server_map, x, y, True, True, link=link_id, item=alive_wood)

    # lianas
    for g in range(0, need_lianas):

        empty_space = 0
        x = 0
        y = 0

        length = 0

        for attempt in range(0, 8):
            x = random.randint(0, map_width - 1)
            y = top_heights[x]

            empty_space = 0

            for y1 in range(y + 1, y + 16):
                b = server_map.get_block(x, y1)
                if b and (not b.blocking()):
                    empty_space = y1
                    break

            if empty_space == 0:
                continue

            server_map.print("Liana at {0}".format(x))

            length = 0

            for y1 in range(empty_space, y + 10):
                b = server_map.get_block(x, y1)
                if b and b.blocking():
                    break
                length += 1

            if length < 4:
                empty_space = 0
            else:
                break

        if empty_space == 0:
            server_map.print("Exhausted all attempts to spawn a liana")
            continue

        length -= 1

        link_id = MapAPI.instance.allocate_link()
        for y1 in range(empty_space, empty_space + length):
            if y1 == empty_space + length - 1:
                code = blocks.LIANA_TRIM
            else:
                code = blocks.LIANA_LEFT if y1 % 2 == 0 else blocks.LIANA_RIGHT
            server_map.set_block(x, y1, BlockSpawn.create_block(liana, code=code, link=link_id), False)

    def find_pattern(x_from, y_from, x_to, y_to, pattern_w, pattern_h, pattern_data) -> Optional[Tuple[int, int]]:
        x_from = max(0, x_from)
        y_from = max(0, y_from)
        x_to = min(server_map.get_width() - pattern_w + 1, x_to)
        y_to = min(server_map.get_height() - pattern_h + 1, y_to)

        for y2 in range(y_from, y_to):
            for x2 in range(x_from, x_to):
                idx = 0
                mismatch = False
                for y3 in range(0, pattern_h):
                    for x3 in range(0, pattern_w):
                        cell = pattern_data[idx]
                        idx += 1
                        if cell is None:
                            continue
                        b3 = server_map.get_block(x2 + x3, y2 + y3)
                        have_something = (b3 is not None) and (b3.blocking())
                        if have_something != cell:
                            mismatch = True
                            break
                    if mismatch:
                        break
                if not mismatch:
                    return x2, y2
        return None

    # cobwebs

    pattern_left = [
        True, True, True,
        True, False, False,
        True, False, False,
    ]

    pattern_right = [
        True, True, True,
        False, False, True,
        False, False, True,
    ]

    cobweb = items.get("cobweb")
    cobwebs_placed = 0

    for g in range(0, need_cobwebs):
        x = int((g / need_cobwebs) * map_width)
        range_x = max(6, int(map_width / need_cobwebs))

        y = top_heights[x]
        orientations = [
            (True, pattern_right),
            (False, pattern_left),
        ]
        random.shuffle(orientations)

        for right, pattern in orientations:
            m = find_pattern(x, y, x + range_x * 2, server_map.get_height() - 2, 3, 3, pattern)
            if m is None:
                continue

            px1, py1 = m
            if right:
                web_x, web_y = px1, py1 + 1
            else:
                web_x, web_y = px1 + 1, py1 + 1

            link_id = MapAPI.instance.allocate_link()

            if Superstructure.SUPERSTRUCTURES["cobweb_left" if right else "cobweb_right"].apply(
                    server_map, web_x, web_y, True, False, link=link_id, item=cobweb):
                cobwebs_placed += 1
                break

    if cobwebs_placed:
        server_map.print("placed {0} cobwebs".format(cobwebs_placed))

    flower = items.get("flower")

    for g in range(0, need_flowers):
        x = random.randint(0, map_width - 1)

        y = top_heights[x]
        b = server_map.get_block(x, y)
        if not isinstance(server_map.get_block(x, y), NeighboringBlockObject):
            continue

        link_id = MapAPI.instance.allocate_link()

        Superstructure.SUPERSTRUCTURES["flower"].apply(
            server_map, x, y, True, True, link=link_id, item=flower)

    rock = items.get("rock")

    for g in range(0, need_rocks):
        x = random.randint(0, map_width - 1)

        y = top_heights[x]
        if not isinstance(server_map.get_block(x, y), NeighboringBlockObject):
            continue

        link_id = MapAPI.instance.allocate_link()

        rocks = ["rock1", "rock2", "rock3"]

        Superstructure.SUPERSTRUCTURES[random.choice(rocks)].apply(
            server_map, x, y, True, True, link=link_id, item=rock)
