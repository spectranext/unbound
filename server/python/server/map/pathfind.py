from collections import deque
from typing import Dict, List, Optional, Tuple

from .. api.map import MapAPI


Point = Tuple[int, int]


def can_path_through(server_map: MapAPI, x: int, y: int) -> bool:
    if x < 0 or y < 0 or x >= server_map.get_width() or y >= server_map.get_height():
        return False
    block = server_map.get_block(x, y)
    return bool(block is not None and not block.blocking())


def find_nearest_open(server_map: MapAPI, origin: Point, max_radius: int = 4) -> Optional[Point]:
    if can_path_through(server_map, origin[0], origin[1]):
        return origin

    for radius in range(1, max_radius + 1):
        for y in range(origin[1] - radius, origin[1] + radius + 1):
            for x in range(origin[0] - radius, origin[0] + radius + 1):
                if abs(x - origin[0]) != radius and abs(y - origin[1]) != radius:
                    continue
                if can_path_through(server_map, x, y):
                    return x, y

    return None


def find_path(
        server_map: MapAPI,
        start: Point,
        target: Point,
        margin_x: int = 40,
        margin_y: int = 25) -> Optional[List[Point]]:
    if start == target:
        return []
    if not can_path_through(server_map, start[0], start[1]):
        return None
    if not can_path_through(server_map, target[0], target[1]):
        return None

    min_x = max(0, min(start[0], target[0]) - margin_x)
    max_x = min(server_map.get_width() - 1, max(start[0], target[0]) + margin_x)
    min_y = max(0, min(start[1], target[1]) - margin_y)
    max_y = min(server_map.get_height() - 1, max(start[1], target[1]) + margin_y)

    queue = deque([start])
    came_from: Dict[Point, Optional[Point]] = {start: None}

    while queue:
        current = queue.popleft()
        if current == target:
            break

        for next_point in (
                (current[0] + 1, current[1]),
                (current[0] - 1, current[1]),
                (current[0], current[1] + 1),
                (current[0], current[1] - 1)):
            if next_point in came_from:
                continue
            if next_point[0] < min_x or next_point[0] > max_x:
                continue
            if next_point[1] < min_y or next_point[1] > max_y:
                continue
            if not can_path_through(server_map, next_point[0], next_point[1]):
                continue
            came_from[next_point] = current
            queue.append(next_point)

    if target not in came_from:
        return None

    path = []
    current = target
    while current != start:
        path.append(current)
        previous = came_from[current]
        if previous is None:
            break
        current = previous
    path.reverse()
    return path


def can_reach(
        server_map: MapAPI,
        start: Point,
        target: Point,
        margin_x: int = 40,
        margin_y: int = 25) -> bool:
    if start == target:
        return True
    if not can_path_through(server_map, start[0], start[1]):
        return False
    if not can_path_through(server_map, target[0], target[1]):
        return False

    min_x = max(0, min(start[0], target[0]) - margin_x)
    max_x = min(server_map.get_width() - 1, max(start[0], target[0]) + margin_x)
    min_y = max(0, min(start[1], target[1]) - margin_y)
    max_y = min(server_map.get_height() - 1, max(start[1], target[1]) + margin_y)

    queue = deque([start])
    seen = {start}

    while queue:
        current = queue.popleft()
        for next_point in (
                (current[0] + 1, current[1]),
                (current[0] - 1, current[1]),
                (current[0], current[1] + 1),
                (current[0], current[1] - 1)):
            if next_point in seen:
                continue
            if next_point[0] < min_x or next_point[0] > max_x:
                continue
            if next_point[1] < min_y or next_point[1] > max_y:
                continue
            if not can_path_through(server_map, next_point[0], next_point[1]):
                continue
            if next_point == target:
                return True
            seen.add(next_point)
            queue.append(next_point)

    return False
