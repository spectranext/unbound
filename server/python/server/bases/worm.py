import random
import functools
from typing import List, Optional, Tuple, TYPE_CHECKING, Dict

from . import BaseInstance, BaseBlockObject, BaseItem
from ..api.block import BlockObject
from .. api.map import MapAPI
from .. import blocks
from .. blockspawn import BlockSpawn
from .. import items
from .. effects import MapEffects

if TYPE_CHECKING:
    from .. team import Team


class WormPathGenerationError(Exception):
    """Exception raised when worm path generation fails."""
    pass


class WormBlockObject(BlockObject):
    """Worm segment tile: non-blocking; participates correctly in light flood-fill.

    Do not override get_light(): refresh.py skips cells when get_light() >= incoming
    brightness; faking max light blocked propagation past worms.

    Override get_code() so encoding ignores ambient light: worm tile IDs have no
    BLOCKING flag, so the default shaded variants collapse to 0 / blocking-empty."""

    def get_code(self):
        if not self.code:
            return blocks.EMPTY
        return self.code

    def blocking(self):
        return False

    def pass_blocking_light(self, light: int) -> int:
        return light
    
    def pass_light(self, light: int) -> int:
        return light

    def flushable(self):
        return False

    def serialize(self):
        return False


WORM_SPEED = 100
# Explosion at burrow mouth this many ms before the head is placed (map warning)
WORM_EMERGE_DELAY_MS = 200


def _worm_cell_overlaps_player_damage_hitbox(head_x: int, head_y: int, px: int, py: int) -> bool:
    """Whether a map cell lies on the player's damage hitbox (anchor px, py = feet).

    Base footprint is 2 wide × 2 tall completing the usual L: feet row plus upper row.
    """
    return (
        (head_x == px and head_y == py)
        or (head_x == px + 1 and head_y == py)
        or (head_x == px and head_y == py - 1)
        or (head_x == px + 1 and head_y == py - 1)
    )


class WormBaseInstance(BaseInstance):
    def __init__(self, x: int, y: int, prototype: 'BaseItem', team: Optional['Team'], **kwargs):
        super().__init__(x, y, prototype, team, **kwargs)
        
        # Get pre-generated worm data from kwargs
        worm_data = kwargs.get('worm_data')
        if not worm_data:
            # No data provided - initialize empty (will fail in on_init)
            self.segments: List[Tuple[int, int]] = []
            self.path: List[Tuple[int, int]] = []
            self.path_index: int = 0
            self.length: int = 5
            self.entry_point: Optional[Tuple[int, int]] = None
            self.shrinking: bool = False
            return
        
        # Use pre-generated data
        self.path: List[Tuple[int, int]] = worm_data['path']
        self.entry_point: Tuple[int, int] = worm_data['entry_point']
        self.length: int = worm_data['length']
        self.segments: List[Tuple[int, int]] = []
        self.path_index: int = 0
        self.shrinking: bool = False
        self.has_damaged_player: bool = False  # Track if we've already damaged a player
        
        # Find entry_point index in path
        for i, pos in enumerate(self.path):
            if pos == self.entry_point:
                self.path_index = i
                break

    def _calculate_block_code(self, segment_index: int, segments: List[Tuple[int, int]]) -> int:
        """Calculate the correct block code for a segment based on path positions.
        
        Path structure: [0=entry ground, 1=entry point, ..., N-1=exit point, N=exit ground]
        Worm moves from path[1] to path[N-1]
        Each segment corresponds to a path position: segments[i] is at path[path_index - i]
        """
        is_full_length = len(segments) == self.length
        current_seg = segments[segment_index]
        
        # Calculate path index for this segment
        # segments[0] (head or first body if head removed) is at path[path_index]
        # segments[i] is at path[path_index - i]
        path_idx = self.path_index - segment_index
        
        # Ensure path_idx is within valid bounds
        if path_idx < 0:
            path_idx = 0
        if path_idx >= len(self.path):
            path_idx = len(self.path) - 1
        
        # During shrinking, treat segment_index == 0 as body (head is hidden but still exists)
        if segment_index == 0 and not self.shrinking:  # Head (only when not shrinking)
            # Head looks at previous path position
            if path_idx > 0:
                prev_path_pos = self.path[path_idx - 1]
                dx = current_seg[0] - prev_path_pos[0]
                dy = current_seg[1] - prev_path_pos[1]
                if dx > 0: return blocks.WORM_HEAD_RIGHT
                elif dx < 0: return blocks.WORM_HEAD_LEFT
                elif dy > 0: return blocks.WORM_HEAD_BOTTOM
                else: return blocks.WORM_HEAD_TOP
            
            # Fallback - default to horizontal
            return blocks.WORM_LEFT_RIGHT_A
        
        elif segment_index == len(segments) - 1:  # Last segment
            # Tail looks at next path position (where it's going/digging into)
            if self.shrinking or is_full_length:
                # Tail - use next path position
                if path_idx < len(self.path) - 1:
                    next_path_pos = self.path[path_idx + 1]
                    # Direction from current to next (where tail is going)
                    dx = next_path_pos[0] - current_seg[0]
                    dy = next_path_pos[1] - current_seg[1]
                    # Tail points in REVERSED direction (opposite of where it's going)
                    if dx > 0: return blocks.WORM_TAIL_LEFT  # Going right, tail points left
                    elif dx < 0: return blocks.WORM_TAIL_RIGHT  # Going left, tail points right
                    elif dy > 0: return blocks.WORM_TAIL_TOP  # Going bottom, tail points top
                    else: return blocks.WORM_TAIL_BOTTOM  # Going top, tail points bottom
                # If no next position, use previous to determine direction
                elif path_idx > 0:
                    prev_path_pos = self.path[path_idx - 1]
                    dx = current_seg[0] - prev_path_pos[0]
                    dy = current_seg[1] - prev_path_pos[1]
                    # Reverse the direction for tail
                    if dx > 0: return blocks.WORM_TAIL_LEFT
                    elif dx < 0: return blocks.WORM_TAIL_RIGHT
                    elif dy > 0: return blocks.WORM_TAIL_TOP
                    else: return blocks.WORM_TAIL_BOTTOM
                # Fallback
                return blocks.WORM_TAIL_BOTTOM
            else:
                # Not at full length yet - treat as body (use both prev and next to detect corners)
                prev_path_pos = self.path[path_idx - 1] if path_idx > 0 else None
                next_path_pos = self.path[path_idx + 1] if path_idx < len(self.path) - 1 else None
                
                # Special case: if at exit point, use exit ground as next
                exit_point_index = len(self.path) - 2 if len(self.path) >= 2 else -1
                if exit_point_index >= 0 and path_idx == exit_point_index and len(self.path) > exit_point_index + 1:
                    next_path_pos = self.path[-1]
                
                # If we have both, check for corner
                if prev_path_pos is not None and next_path_pos is not None:
                    prev_dx = current_seg[0] - prev_path_pos[0]
                    prev_dy = current_seg[1] - prev_path_pos[1]
                    next_dx = next_path_pos[0] - current_seg[0]
                    next_dy = next_path_pos[1] - current_seg[1]
                    
                    # Check if corner
                    if (prev_dx, prev_dy) != (next_dx, next_dy):
                        # Corner - use same logic as body
                        sides = set()
                        if prev_dx > 0: sides.add("left")
                        elif prev_dx < 0: sides.add("right")
                        if prev_dy > 0: sides.add("top")
                        elif prev_dy < 0: sides.add("bottom")
                        if next_dx > 0: sides.add("right")
                        elif next_dx < 0: sides.add("left")
                        if next_dy > 0: sides.add("bottom")
                        elif next_dy < 0: sides.add("top")
                        
                        if "left" in sides and "top" in sides: return blocks.WORM_TOP_LEFT
                        elif "left" in sides and "bottom" in sides: return blocks.WORM_BOTTOM_LEFT
                        elif "right" in sides and "top" in sides: return blocks.WORM_TOP_RIGHT
                        elif "right" in sides and "bottom" in sides: return blocks.WORM_BOTTOM_RIGHT
                
                # Straight segment - use previous direction
                if prev_path_pos is not None:
                    dx = current_seg[0] - prev_path_pos[0]
                    dy = current_seg[1] - prev_path_pos[1]
                    if dx != 0:  # Horizontal
                        return blocks.WORM_LEFT_RIGHT_A if current_seg[0] % 2 == 0 else blocks.WORM_LEFT_RIGHT_B
                    else:  # Vertical
                        return blocks.WORM_UP_DOWN_A if current_seg[1] % 2 == 0 else blocks.WORM_UP_DOWN_B
                return blocks.WORM_LEFT_RIGHT_A
        
        else:  # Body (or segment_index == 0 during shrinking)
            # Body always uses path positions for calculation
            # Get previous and next path positions
            prev_path_pos = self.path[path_idx - 1] if path_idx > 0 else None
            next_path_pos = self.path[path_idx + 1] if path_idx < len(self.path) - 1 else None
            
            # Special case: if at exit point (N-1), always use exit ground (N) as next
            exit_point_index = len(self.path) - 2 if len(self.path) >= 2 else -1
            if exit_point_index >= 0 and path_idx == exit_point_index and len(self.path) > exit_point_index + 1:
                next_path_pos = self.path[-1]  # Exit ground block
            
            # Calculate directions from path positions
            # prev_dx/dy: direction FROM previous TO current
            # next_dx/dy: direction FROM current TO next
            if prev_path_pos is not None and next_path_pos is not None:
                prev_dx = current_seg[0] - prev_path_pos[0]
                prev_dy = current_seg[1] - prev_path_pos[1]
                next_dx = next_path_pos[0] - current_seg[0]
                next_dy = next_path_pos[1] - current_seg[1]
            elif prev_path_pos is not None:
                # Only have previous - assume straight (use previous direction for both)
                prev_dx = current_seg[0] - prev_path_pos[0]
                prev_dy = current_seg[1] - prev_path_pos[1]
                next_dx = prev_dx
                next_dy = prev_dy
            elif next_path_pos is not None:
                # Only have next - assume straight (use next direction for both)
                next_dx = next_path_pos[0] - current_seg[0]
                next_dy = next_path_pos[1] - current_seg[1]
                prev_dx = next_dx
                prev_dy = next_dy
            else:
                # No path positions available - fallback to segment-based (shouldn't happen normally)
                if segment_index > 0 and segment_index < len(segments) - 1:
                    prev_seg = segments[segment_index - 1]
                    next_seg = segments[segment_index + 1]
                    prev_dx = current_seg[0] - prev_seg[0]
                    prev_dy = current_seg[1] - prev_seg[1]
                    next_dx = next_seg[0] - current_seg[0]
                    next_dy = next_seg[1] - current_seg[1]
                else:
                    # Last resort - assume horizontal
                    prev_dx = 1
                    prev_dy = 0
                    next_dx = 1
                    next_dy = 0
            
            # Check if corner
            if (prev_dx, prev_dy) != (next_dx, next_dy):
                # Corner case - determine which sides connect
                # Coordinate system: left = negative x, right = positive x, top = negative y, bottom = positive y
                # prev_dx/prev_dy: direction we came FROM (prev -> current)
                #   prev_dx > 0 means came from left (moved right)
                #   prev_dx < 0 means came from right (moved left)
                #   prev_dy > 0 means came from top (moved bottom)
                #   prev_dy < 0 means came from bottom (moved top)
                # next_dx/next_dy: direction we're going TO (current -> next)
                #   next_dx > 0 means going right
                #   next_dx < 0 means going left
                #   next_dy > 0 means going bottom
                #   next_dy < 0 means going top
                
                # Collect all directions involved in this corner
                sides = set()
                
                # Incoming direction
                if prev_dx > 0:  # Came from left
                    sides.add("left")
                elif prev_dx < 0:  # Came from right
                    sides.add("right")
                if prev_dy > 0:  # Came from top
                    sides.add("top")
                elif prev_dy < 0:  # Came from bottom
                    sides.add("bottom")
                
                # Outgoing direction
                if next_dx > 0:  # Going right
                    sides.add("right")
                elif next_dx < 0:  # Going left
                    sides.add("left")
                if next_dy > 0:  # Going bottom
                    sides.add("bottom")
                elif next_dy < 0:  # Going top
                    sides.add("top")
                
                # Map to corner block based on which two sides connect
                if "left" in sides and "top" in sides:
                    return blocks.WORM_TOP_LEFT
                elif "left" in sides and "bottom" in sides:
                    return blocks.WORM_BOTTOM_LEFT
                elif "right" in sides and "top" in sides:
                    return blocks.WORM_TOP_RIGHT
                elif "right" in sides and "bottom" in sides:
                    return blocks.WORM_BOTTOM_RIGHT
            
            # Straight segment
            if prev_dx != 0:  # Horizontal
                return blocks.WORM_LEFT_RIGHT_A if segments[segment_index][0] % 2 == 0 else blocks.WORM_LEFT_RIGHT_B
            else:  # Vertical
                return blocks.WORM_UP_DOWN_A if segments[segment_index][1] % 2 == 0 else blocks.WORM_UP_DOWN_B

    def set_block_code(self, x: int, y: int, code: int):
        b = WormBlockObject(code)
        MapAPI.instance.set_block(self.x + x, self.y - y, b, True)

    def _worm_explosion(self, map_x: float, map_y: float):
        MapAPI.instance.send_effect(float(map_x), float(map_y), MapEffects.EXPLOSION)

    def _worm_emerge_and_start(self):
        """Place head at entry after pre-emerge warning; then run the normal move loop."""
        if not self.path or not self.entry_point:
            return
        self.segments = [self.entry_point]
        rel_x = self.entry_point[0] - self.x
        rel_y = self.y - self.entry_point[1]
        head_code = self._calculate_block_code(0, self.segments)
        self.set_block_code(rel_x, rel_y, head_code)
        MapAPI.instance.schedule_callback(functools.partial(self._update_worm), WORM_SPEED)

    def _update_worm(self):
        """Update worm movement - called via scheduled callback every 100ms."""
        if self.shrinking:
            # Shrinking phase: remove tail segments one by one
            if len(self.segments) <= 1:
                self.destroy()
                return
            
            # Remove tail segment
            old_tail = self.segments.pop()
            # Clear old tail block
            MapAPI.instance.set_block(old_tail[0], old_tail[1], BlockSpawn.create_block(items.NOTHING), True)
            
            # Update all segment block codes (including segment_index == 0 which is treated as body)
            for i, (x, y) in enumerate(self.segments):
                new_code = self._calculate_block_code(i, self.segments)
                rel_x = x - self.x
                rel_y = self.y - y
                old_block = self.get_block(rel_x, rel_y)
                if old_block is None or old_block.code != new_code:
                    self.set_block_code(rel_x, rel_y, new_code)
            
            # Schedule next update
            MapAPI.instance.schedule_callback(functools.partial(self._update_worm), WORM_SPEED)
            return
        
        # Normal movement phase
        # Check if we've reached the exit point (path[-2], not the exit ground block at path[-1])
        # The exit ground block is only used for orientation calculations, not actual movement
        exit_point_index = len(self.path) - 2  # Second to last (exit point, not exit ground)
        if self.path_index >= exit_point_index:
            # Reached exit point, start shrinking phase
            self.shrinking = True
            ex, ey = self.path[exit_point_index]
            self._worm_explosion(ex - 0.5, ey + 0.5)
            # Don't remove head - it will be treated as body in calculations
            # Just update all segments to recalculate block codes
            for i, (x, y) in enumerate(self.segments):
                new_code = self._calculate_block_code(i, self.segments)
                rel_x = x - self.x
                rel_y = self.y - y
                old_block = self.get_block(rel_x, rel_y)
                if old_block is None or old_block.code != new_code:
                    self.set_block_code(rel_x, rel_y, new_code)
            
            # Schedule next update
            MapAPI.instance.schedule_callback(functools.partial(self._update_worm), WORM_SPEED)
            return
        
        # Increment path_index first to get the next position
        self.path_index += 1
        new_pos = self.path[self.path_index]
        
        # Check if head crosses path with any player (only once)
        if not self.has_damaged_player:
            head_x, head_y = new_pos
            # Check all clients for player objects at this position
            for client in MapAPI.instance.query_clients():
                player = client.get_client_object()
                if player:
                    px = int(player.get_x())
                    py = int(player.get_y())
                    if _worm_cell_overlaps_player_damage_hitbox(head_x, head_y, px, py):
                        # Damage player once
                        player.damage(25, reason="Worm bite")
                        self.has_damaged_player = True
                        break
        
        if len(self.segments) < self.length:
            # Growing phase: add head, no tail shown yet
            self.segments.insert(0, new_pos)
        else:
            # Full length: remove tail, add head, tail is now visible
            old_tail = self.segments.pop()
            # Clear old tail block
            MapAPI.instance.set_block(old_tail[0], old_tail[1], BlockSpawn.create_block(items.NOTHING), True)
            self.segments.insert(0, new_pos)
        
        # Update all segment block codes
        for i, (x, y) in enumerate(self.segments):
            new_code = self._calculate_block_code(i, self.segments)
            # Calculate relative position from base origin (y is subtracted in set_block_code)
            rel_x = x - self.x
            rel_y = self.y - y
            old_block = self.get_block(rel_x, rel_y)
            if old_block is None or old_block.code != new_code:
                self.set_block_code(rel_x, rel_y, new_code)
        
        # Schedule next update
        MapAPI.instance.schedule_callback(functools.partial(self._update_worm), WORM_SPEED)

    def on_update(self):
        """Called by base update system - we use scheduled callbacks instead."""
        pass

    def destroy(self):
        """Clean up all worm segments before destroying base."""
        # Clear all worm segment blocks
        for x, y in self.segments:
            MapAPI.instance.set_block(x, y, BlockSpawn.create_block(items.NOTHING), True)
        super().destroy()

    def on_init(self):
        """Initialize worm with pre-generated path data."""
        if not self.path or not self.entry_point:
            # No path data - destroy
            self.destroy()
            return
        
        # Warning burst at burrow mouth, then emerge after WORM_EMERGE_DELAY_MS
        ex, ey = self.entry_point
        MapAPI.instance.schedule_callback(
            functools.partial(self._worm_explosion, ex - 0.5, ey + 0.5),
            0,
        )
        MapAPI.instance.schedule_callback(
            functools.partial(self._worm_emerge_and_start),
            WORM_EMERGE_DELAY_MS,
        )


class WormBaseItem(BaseItem):
    def __init__(self, identity: str):
        # Use a spawner function to create WormBaseBlockObject instead of default BaseBlockObject
        def worm_block_spawner(bi: BaseInstance) -> WormBlockObject:
            return WormBlockObject(0, bi)
        
        super().__init__(identity, blocks=[worm_block_spawner], width=1, height=1)
        self.health = 1  # Worms are temporary, low health

    @staticmethod
    def _has_ground_neighbor(x: int, y: int, server_map: MapAPI) -> bool:
        """Check if a block has a ground neighbor."""
        for nx, ny in [(x+1, y), (x-1, y), (x, y+1), (x, y-1)]:
            if nx < 0 or nx >= server_map.get_width() or ny < 0 or ny >= server_map.get_height():
                continue
            neighbor = server_map.get_block(nx, ny)
            if neighbor and neighbor.blocking():
                return True
        return False

    @staticmethod
    def _find_ground_neighbor(x: int, y: int, server_map: MapAPI) -> Optional[Tuple[int, int]]:
        """Find a ground neighbor block. Returns the first blocking neighbor found."""
        for nx, ny in [(x+1, y), (x-1, y), (x, y+1), (x, y-1)]:
            if nx < 0 or nx >= server_map.get_width() or ny < 0 or ny >= server_map.get_height():
                continue
            neighbor = server_map.get_block(nx, ny)
            if neighbor and neighbor.blocking():
                return (nx, ny)
        return None

    @staticmethod
    def _find_entry_point(start_x: int, start_y: int, server_map: MapAPI) -> Tuple[int, int]:
        """Find the closest empty block with a ground neighbor."""
        search_radius = 10
        
        best_point = None
        best_dist = float('inf')
        
        # Search nearby blocks
        for dx in range(-search_radius, search_radius + 1):
            for dy in range(-search_radius, search_radius + 1):
                x = start_x + dx
                y = start_y + dy
                
                # Check bounds
                if x < 0 or x >= server_map.get_width() or y < 0 or y >= server_map.get_height():
                    continue
                
                # Check if block is empty
                block = server_map.get_block(x, y)
                if block and block.blocking():
                    continue
                
                # Check if block has ground neighbor
                if WormBaseItem._has_ground_neighbor(x, y, server_map):
                    dist = abs(dx) + abs(dy)  # Manhattan distance
                    if dist < best_dist:
                        best_dist = dist
                        best_point = (x, y)
        
        return best_point if best_point else (start_x, start_y)

    @staticmethod
    def _get_player_positions(server_map: MapAPI) -> List[Tuple[int, int]]:
        """Get all player positions as (x, y) tuples."""
        positions = []
        for client in server_map.query_clients():
            player = client.get_client_object()
            if player:
                px = int(player.get_x())
                py = int(player.get_y())
                positions.append((px, py))
        return positions

    @staticmethod
    def _calculate_path_score(path: List[Tuple[int, int]], priority: str, player_positions: List[Tuple[int, int]]) -> float:
        """Calculate score for a path based on priority. Higher score = better."""
        path_len = len(path)
        if path_len == 0:
            return 0.0

        score = 0.0
        for i, pos in enumerate(path):
            px, py = pos
            hits_player = False
            for player_pos in player_positions:
                ppx, ppy = player_pos
                if _worm_cell_overlaps_player_damage_hitbox(px, py, ppx, ppy):
                    hits_player = True
                    break
            if not hits_player:
                continue

            if priority == "avoid_player":
                score -= 100.0
            elif priority == "cross_player":
                # Weight by proximity to mid-path: 0.0 at ends, 1.0 at center
                t = i / (path_len - 1) if path_len > 1 else 0.5
                mid_weight = 1.0 - abs(2.0 * t - 1.0)
                score += mid_weight * 100.0

        if priority == "avoid_player":
            score += 1000.0

        return score

    @staticmethod
    def _generate_path(start_x: int, start_y: int, server_map: MapAPI, priority: str = "cross_player") -> Optional[List[Tuple[int, int]]]:
        """Generate path using flood fill algorithm."""
        # Entry point from _find_entry_point
        origin = WormBaseItem._find_entry_point(start_x, start_y, server_map)
        
        # Get player positions for scoring
        player_positions = WormBaseItem._get_player_positions(server_map)
        
        min_length = 20
        max_length = 32
        
        # Flood fill from origin
        visited = {}  # (x, y) -> (distance, last_dir, straight_count, parent)
        queue = [(origin[0], origin[1], 0, None, 0, None)]  # (x, y, dist, last_dir, straight_count, parent)
        candidates = []  # (exit_point, path) tuples for scoring
        
        while queue:
            x, y, dist, last_dir, straight_count, parent = queue.pop(0)
            
            # Check bounds
            if x < 0 or x >= server_map.get_width() or y < 0 or y >= server_map.get_height():
                continue
            
            if dist > max_length:
                continue
            
            if (x, y) in visited:
                continue
            
            block = server_map.get_block(x, y)
            if block and block.code != 0:
                continue  # Skip non-empty blocks
            
            visited[(x, y)] = (dist, last_dir, straight_count, parent)
            
            # Collect all candidates within valid range (8-32 blocks) that have ground neighbors
            if min_length <= dist <= max_length:
                # Only add as candidate if it has a ground neighbor (exit point requirement)
                if WormBaseItem._has_ground_neighbor(x, y, server_map):
                    # Reconstruct path from origin to this candidate
                    path = []
                    current = (x, y)
                    while current is not None:
                        path.append(current)
                        if current == origin:
                            break
                        _, _, _, parent = visited[current]
                        current = parent
                    path.reverse()
                    
                    # Add ground blocks at start and end for correct orientation
                    entry_ground = WormBaseItem._find_ground_neighbor(origin[0], origin[1], server_map)
                    if entry_ground:
                        path.insert(0, entry_ground)
                    exit_ground = WormBaseItem._find_ground_neighbor(x, y, server_map)
                    if exit_ground:
                        path.append(exit_ground)
                    
                    # Calculate score for this path
                    score = WormBaseItem._calculate_path_score(path, priority, player_positions)
                    candidates.append((score, path))
            
            # Continue exploring even if we found a candidate (to find more options)
            
            # Explore neighbors
            for dx, dy in [(0, 1), (0, -1), (1, 0), (-1, 0)]:
                nx, ny = x + dx, y + dy
                
                # Check bounds
                if nx < 0 or nx >= server_map.get_width() or ny < 0 or ny >= server_map.get_height():
                    continue
                
                if (nx, ny) in visited:
                    continue
                
                # Determine if this is a turn
                is_turn = False
                if last_dir is not None:
                    prev_dx, prev_dy = last_dir
                    if (dx, dy) != (prev_dx, prev_dy) and (dx, dy) != (-prev_dx, -prev_dy):
                        is_turn = True
                
                new_straight = 1 if is_turn else straight_count + 1
                
                # Enforce: after turn, must go straight >= 1 block
                if is_turn and straight_count < 1:
                    continue
                
                queue.append((nx, ny, dist + 1, (dx, dy), new_straight, (x, y)))
        
        if not candidates:
            raise WormPathGenerationError("No suitable exit path found")
        
        # Sort candidates by score (highest first)
        candidates.sort(key=lambda x: x[0], reverse=True)
        
        # Pick the best path (highest score)
        _, path = candidates[0]
        
        return path

    @staticmethod
    def generate_worm_data(x: int, y: int, server_map: MapAPI, priority: str = "cross_player") -> Dict:
        """Generate all worm data needed for creation. Raises WormPathGenerationError if generation fails.
        
        Args:
            priority: "cross_player" to prefer paths that cross players,
                     "avoid_player" to prefer paths that don't cross players
        """
        # Generate path then reverse so worm travels from exit toward entry,
        # giving the player more warning before the crossing
        path = WormBaseItem._generate_path(x, y, server_map, priority)
        path.reverse()
        
        # Find entry point (should be at index 1 if ground block was added at start, otherwise index 0)
        entry_point = None
        for i, pos in enumerate(path):
            # Check if this position has a ground neighbor (entry point requirement)
            if WormBaseItem._has_ground_neighbor(pos[0], pos[1], server_map):
                # Check if it's not a blocking block itself
                block = server_map.get_block(pos[0], pos[1])
                if not block or not block.blocking():
                    entry_point = pos
                    break
        
        if not entry_point:
            # Fallback: use first non-ground block in path
            for pos in path:
                block = server_map.get_block(pos[0], pos[1])
                if not block or not block.blocking():
                    entry_point = pos
                    break
        
        if not entry_point:
            raise WormPathGenerationError("No valid entry point found")
        
        # Generate random length
        length = random.randint(5, 10)
        
        return {
            'path': path,
            'entry_point': entry_point,
            'length': length
        }

    @staticmethod
    def track_block_removed(player, x: int, y: int, item):
        """Track when a block is removed by a player. Called from hit.py."""
        try:
            from .. bot.worms import WormDiggingTracker
            player_id = player.get_client_id()
            WormDiggingTracker.track_block_removal_static(player_id, x, y, item)
        except Exception:
            # Silently fail if tracker not available or error occurs
            pass

    def get_instance(self, x: int, y: int, team: Optional['Team'], **kwargs) -> WormBaseInstance:
        return WormBaseInstance(x, y, self, team, **kwargs)
