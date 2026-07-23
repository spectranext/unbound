import math
from typing import TYPE_CHECKING, Optional

from .. api.map import MapAPI
from .. scenarios import get_scenario
from .. items.generation import Generation

if TYPE_CHECKING:
    from .. scenarios import Scenario


def lower_than_ground_line(x_coord: int, y_coord: int, line: float, angle: float, scenario: 'Scenario') -> bool:
    """Check if coordinates are below the ground line.
    
    Args:
        x_coord: X coordinate
        y_coord: Y coordinate
        line: Base line value (e.g., Generation.MIDDLE_LINE)
        angle: Map generation angle
        scenario: Scenario object with map_wave_a and map_wave_b attributes
        
    Returns:
        True if y_coord is at or below the ground line
    """
    ajd_angle = angle + x_coord / 16
    compare_to = line + (math.cos(ajd_angle) * scenario.map_wave_a) + \
                 (math.cos(ajd_angle + x_coord / 24) * scenario.map_wave_b)
    return y_coord >= compare_to


def get_surface_y(server_map: MapAPI, x: int) -> Optional[float]:
    """Get the Y coordinate of the surface line at a given X coordinate.
    
    Args:
        server_map: MapAPI instance
        x: X coordinate
        
    Returns:
        Surface Y coordinate, or None if angle not set
    """
    if not hasattr(server_map, 'map_generation_angle') or server_map.map_generation_angle is None:
        return None
    
    scenario = get_scenario(server_map.scenario)
    angle = server_map.map_generation_angle
    ajd_angle = angle + x / 16
    surface_y = Generation.MIDDLE_LINE + (math.cos(ajd_angle) * scenario.map_wave_a) + \
                (math.cos(ajd_angle + x / 24) * scenario.map_wave_b)
    return surface_y


def is_below_surface(server_map: MapAPI, x: int, y: int) -> bool:
    """Check if a block position is below the surface line.
    
    Args:
        server_map: MapAPI instance
        x: X coordinate
        y: Y coordinate
        
    Returns:
        True if the position is below the surface line
    """
    if not hasattr(server_map, 'map_generation_angle') or server_map.map_generation_angle is None:
        # If angle not set, assume not below surface (safety check)
        return False
    
    scenario = get_scenario(server_map.scenario)
    return lower_than_ground_line(x, y, Generation.MIDDLE_LINE, server_map.map_generation_angle, scenario)


def is_at_least_blocks_below_surface(server_map: MapAPI, x: int, y: int, blocks: int = 5) -> bool:
    """Check if a block position is at least N blocks below the surface line.
    
    Args:
        server_map: MapAPI instance
        x: X coordinate
        y: Y coordinate
        blocks: Number of blocks below surface required (default: 5)
        
    Returns:
        True if the position is at least N blocks below the surface line
    """
    surface_y = get_surface_y(server_map, x)
    if surface_y is None:
        return False
    
    # Check if y is at least 'blocks' units below the surface
    return y >= surface_y + blocks
