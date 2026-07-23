from typing import TYPE_CHECKING
from . cmd import TerminalError, TerminalCommand
from ... api.map import MapAPI
from ... bases.worm import WormBaseItem, WormPathGenerationError
from ... bases import spawn_base

if TYPE_CHECKING:
    from .. client import Client


class Worm(TerminalCommand):
    def apply(self, c: 'Client', *args: str) -> str:
        if c.player is None:
            raise TerminalError("Not spawned")
        
        player = c.get_client_object()
        if not player:
            raise TerminalError("Not spawned")
        
        # Find nearby empty location (similar to slime spawn logic)
        px = int(player.get_x())
        py = int(player.get_y())
        
        # Try to find a good spawn location nearby
        spawn_x, spawn_y = px, py
        
        # Parse priority parameter (default: "avoid_player")
        priority = "avoid_player"
        if args and len(args) > 0:
            priority_arg = args[0].lower()
            if priority_arg in ("avoid_player", "cross_player"):
                priority = priority_arg
            else:
                raise TerminalError("Priority must be 'avoid_player' or 'cross_player'")
        
        # Create worm base item if it doesn't exist
        worm_item = WormBaseItem("worm")
        
        # Generate worm data first (path, entry point, etc.)
        try:
            worm_data = WormBaseItem.generate_worm_data(spawn_x, spawn_y, MapAPI.instance, priority)
        except WormPathGenerationError as e:
            raise TerminalError(str(e))
        
        # Spawn worm base nearby with pre-generated data
        bi = spawn_base(worm_item, spawn_x, spawn_y, c.get_team(), worm_data=worm_data)
        bi.tag = MapAPI.instance.allocate_tag()
        bi.on_init()
        
        return "OK"
    
    def minimum_args(self) -> int:
        return 0
    
    def required_role(self) -> int:
        return 1
