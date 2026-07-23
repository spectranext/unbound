from typing import Dict, TYPE_CHECKING

from . cmd import TerminalCommand, TerminalError
from . give import Give
from . debug import Debug
from . tp import Teleport
from . kill import Kill
from . slime import Slime
from . spider import Spider
from . bot import Bot
from . dev import Dev
from . cpu import CPU
from . temp import Temperature
from . god import God
from . worm import Worm
from . report import Report
from . mark import Mark
from . goto import Goto

if TYPE_CHECKING:
    from .. client import Client


COMMANDS: Dict[str, TerminalCommand] = {
    "give": Give(),
    "debug": Debug(),
    "tp": Teleport(),
    "kill": Kill(),
    "slime": Slime(),
    "spider": Spider(),
    "bot": Bot(),
    "dev": Dev(),
    "cpu": CPU(),
    "temp": Temperature(),
    "god": God(),
    "worm": Worm(),
    "report": Report(),
    "mark": Mark(),
    "goto": Goto(),
}


def apply_terminal_command(c: 'Client', s: str) -> str:
    args = s.split(" ")
    if len(args) == 0:
        raise TerminalError("No command specified")
    c1 = args[0]
    cmd_args = args[1:]
    if c1 not in COMMANDS:
        raise TerminalError("Unknown command: {0}".format(c1))
    if c.role < COMMANDS[c1].required_role():
        raise TerminalError("Command not permitted: {0}".format(c1))
    if len(cmd_args) < COMMANDS[c1].minimum_args():
        raise TerminalError("Not enough arguments (required {0})".format(COMMANDS[c1].minimum_args()))
    return COMMANDS[c1].apply(c, *cmd_args)
