import time
from typing import Dict, Optional

from .. api.map import MapAPI
from .. api.object import ObjectAPI
from .. bases.worm import WormBaseItem, WormPathGenerationError
from .. bases import spawn_base
from .. map.level import is_at_least_blocks_below_surface
import random


class PlayerDiggingData:
    """Tracks digging score, decay, and cooldown for a player."""
    
    def __init__(self):
        self.score: float = 0.0
        self.last_update_time: float = time.time()
        self.cooldown_until: float = 0.0
    
    def add_score(self, points: float, current_time: float):
        """Add points to score and update timestamp."""
        self.score += points
        self.last_update_time = current_time
    
    def decay(self, current_time: float):
        """Apply decay based on elapsed time (1 point per second)."""
        elapsed = current_time - self.last_update_time
        if elapsed > 0:
            self.score = max(0.0, self.score - elapsed)
            self.last_update_time = current_time
    
    def is_on_cooldown(self, current_time: float) -> bool:
        """Check if cooldown is active."""
        return current_time < self.cooldown_until
    
    def set_cooldown(self, duration: float, current_time: float):
        """Set cooldown duration."""
        self.cooldown_until = current_time + duration


class WormDiggingTracker:
    """Service that tracks player digging and spawns player-seeking worms."""
    
    _instance: Optional['WormDiggingTracker'] = None
    _player_data: Dict[int, PlayerDiggingData] = {}
    
    ACTIVATION_THRESHOLD = 50.0
    MAX_THRESHOLD = 200.0  # Maximum score - worms stop spawning above this
    DECAY_RATE = 1.0  # points per second
    COOLDOWN_DURATION = 8.0  # seconds
    
    @classmethod
    def get_instance(cls) -> 'WormDiggingTracker':
        """Get singleton instance."""
        if cls._instance is None:
            cls._instance = cls()
        return cls._instance
    
    def _get_player_data(self, player_id: int) -> PlayerDiggingData:
        """Get or create player data."""
        if player_id not in self._player_data:
            self._player_data[player_id] = PlayerDiggingData()
        return self._player_data[player_id]
    
    def track_block_removal(self, player_id: int, x: int, y: int, item):
        """Track when a block is removed by a player."""
        server_map = MapAPI.instance
        if not server_map:
            return
        
        # Check if block is at least 5 blocks below surface
        if is_at_least_blocks_below_surface(server_map, x, y, 5):
            # Use item's worm_trigger value (default 0)
            points = item.worm_trigger if hasattr(item, 'worm_trigger') else 0
            if points > 0:
                current_time = time.time()
                player_data = self._get_player_data(player_id)
                player_data.add_score(points, current_time)
    
    def _find_spawn_point_near_player(self, player: ObjectAPI, server_map: MapAPI) -> Optional[tuple]:
        """Find a spawn point 5-10 blocks away from player that is empty and has ground nearby."""
        px = int(player.get_x())
        py = int(player.get_y())
        
        # Try to find a suitable spawn point
        min_distance = 5
        max_distance = 10
        max_attempts = 50
        
        for _ in range(max_attempts):
            # Pick random offset ensuring distance is between min_distance and max_distance
            # Use Manhattan distance (sum of absolute x and y offsets)
            dx = random.randint(-max_distance, max_distance)
            dy = random.randint(-max_distance, max_distance)
            
            # Ensure minimum Manhattan distance
            manhattan_dist = abs(dx) + abs(dy)
            if manhattan_dist < min_distance or manhattan_dist > max_distance:
                continue
            
            spawn_x = px + dx
            spawn_y = py + dy
            
            # Check bounds
            if spawn_x < 0 or spawn_x >= server_map.get_width() or spawn_y < 0 or spawn_y >= server_map.get_height():
                continue
            
            # Check if block is empty
            block = server_map.get_block(spawn_x, spawn_y)
            if block and block.blocking():
                continue
            
            # Check if block has ground neighbor (required for worm entry point)
            if WormBaseItem._has_ground_neighbor(spawn_x, spawn_y, server_map):
                return (spawn_x, spawn_y)
        
        # Fallback: use player position if no suitable point found
        return (px, py)
    
    def _spawn_worm(self, player: ObjectAPI, priority: str, server_map: MapAPI):
        """Spawn a worm near the player."""
        try:
            # Find a spawn point 5-10 blocks away from player
            spawn_point = self._find_spawn_point_near_player(player, server_map)
            if not spawn_point:
                return
            
            spawn_x, spawn_y = spawn_point
            
            # Get player's team
            client = player.get_client()
            team = client.get_team() if client else None
            
            # Create worm base item
            worm_item = WormBaseItem("worm")
            
            # Generate worm data using the spawn point
            worm_data = WormBaseItem.generate_worm_data(spawn_x, spawn_y, server_map, priority)
            
            # Spawn worm base at the spawn point
            bi = spawn_base(worm_item, spawn_x, spawn_y, team, worm_data=worm_data)
            bi.tag = server_map.allocate_tag()
            bi.on_init()
            
        except WormPathGenerationError as e:
            # Log error but don't crash
            server_map.print("Failed to spawn worm: {0}".format(str(e)))
        except Exception as e:
            # Catch any other errors
            server_map.print("Error spawning worm: {0}".format(str(e)))
    
    def update(self, server_map: MapAPI):
        """Update tracker - called periodically (every second)."""
        current_time = time.time()
        
        # Decay scores and check thresholds
        for player_id, player_data in list(self._player_data.items()):
            # Apply decay
            player_data.decay(current_time)
            
            # Skip if on cooldown
            if player_data.is_on_cooldown(current_time):
                continue
            
            # Check thresholds
            score = player_data.score
            
            # Don't spawn worms if score exceeds max threshold
            if score >= self.MAX_THRESHOLD:
                continue
            
            if score >= self.ACTIVATION_THRESHOLD:
                # Spawn attack worms continuously while above threshold.
                player = self._get_player_object(player_id, server_map)
                if player:
                    self._spawn_worm(player, "cross_player", server_map)
                    player_data.set_cooldown(self.COOLDOWN_DURATION, current_time)
    
    def _get_player_object(self, player_id: int, server_map: MapAPI) -> Optional[ObjectAPI]:
        """Get player object by client ID."""
        client = server_map.get_client(player_id)
        if client:
            return client.get_client_object()
        return None
    
    @classmethod
    def track_block_removal_static(cls, player_id: int, x: int, y: int, item):
        """Static method to track block removal."""
        instance = cls.get_instance()
        instance.track_block_removal(player_id, x, y, item)
