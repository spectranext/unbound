import asyncio
import base64
import os
import zlib

from ...api.map import MapAPI
from .team import handle_upload, parse_team, status_message

TEMPLATE_NAME = "web/computer.html"
FRAME_INTERVAL_SECONDS = 0.1
SCREEN_MEMORY_OFFSET = 0x4000
SCREEN_MEMORY_SIZE = 6912
KEYBOARD_ROWS = (0xFE, 0xFD, 0xFB, 0xF7, 0xEF, 0xDF, 0xBF, 0x7F)


def parse_computer(arguments: dict):
    computer_hash = arguments.get("computer_hash")
    if not isinstance(computer_hash, str) or not computer_hash:
        raise ValueError("Invalid computer hash")

    computer = MapAPI.instance.computer_find(computer_hash.encode())
    if computer is None:
        raise ValueError("Unknown computer")

    return computer


def apply_command(computer, command: str | None) -> str | None:
    if command is None:
        return None
    if command == "power_on":
        computer.set_power(True)
        return "Computer powered on"
    if command == "power_off":
        computer.set_power(False)
        return "Computer powered off"
    if command == "reboot":
        computer.reboot()
        return "Computer rebooted"
    if command == "nmi":
        computer.nmi()
        return "NMI triggered"
    raise ValueError("Unknown command")


def build_context(auth, team, computer, status: str | None = None):
    computer_hash = computer.get_hash().decode()
    powered_on = computer.is_powered_on()
    base_url = (
        f"/web/{auth['server_hash']}/computer"
        f"?token={auth['token']}"
        f"&team_id={team.team_id}"
        f"&computer_hash={computer_hash}"
    )
    return {
        "auth": auth,
        "team": team,
        "server_title": os.environ.get("REPORT_NAME", "Server"),
        "computer_name": computer.get_hostname().decode(errors="replace"),
        "computer_hash": computer_hash,
        "clean_url": base_url,
        "powered_on": powered_on,
        "status": status,
        "actions": [
            {"label": "Reboot", "href": f"{base_url}&command=reboot"},
            {"label": "NMI", "href": f"{base_url}&command=nmi"},
        ],
    }


async def frame_pump(computer, send_message):
    last_payload = None
    last_powered_on = None

    while True:
        started = asyncio.get_running_loop().time()
        powered_on = computer.is_powered_on()

        if not powered_on:
            if last_powered_on is not False:
                await send_message({
                    "type": "screen_state",
                    "powered_on": False,
                    "border": 0,
                })
                last_payload = None
                last_powered_on = False
            elapsed = asyncio.get_running_loop().time() - started
            await asyncio.sleep(max(0.0, FRAME_INTERVAL_SECONDS - elapsed))
            continue

        memory = computer.get_memory(SCREEN_MEMORY_OFFSET, SCREEN_MEMORY_SIZE)
        border = computer.get_ula() & 0x07
        payload = (memory, border)

        if payload != last_payload or last_powered_on is not True:
            compressed = zlib.compress(memory)
            await send_message({
                "type": "screen_frame",
                "powered_on": True,
                "border": border,
                "memory_base64": base64.b64encode(compressed).decode("ascii"),
            })
            last_payload = payload
            last_powered_on = True

        elapsed = asyncio.get_running_loop().time() - started
        await asyncio.sleep(max(0.0, FRAME_INTERVAL_SECONDS - elapsed))


def make_log_handler(send_message, loop):
    def on_log_message(message: bytes):
        loop.call_soon_threadsafe(asyncio.create_task, send_message({
            "type": "log",
            "message": message.decode(errors="replace"),
        }))
    return on_log_message


async def get(arguments, auth):
    team = parse_team(arguments)
    computer = parse_computer(arguments)
    if computer not in MapAPI.instance.query_team_computers(team.team_id):
        raise ValueError("Computer does not belong to team")

    status = apply_command(computer, arguments.get("command"))
    return {
        "template": TEMPLATE_NAME,
        "context": build_context(auth, team, computer, status),
    }


async def post(arguments, content, auth):
    return await get(arguments, auth)


class ComputerSession:
    async def open(self, arguments, auth, send_message, session_id):
        team = parse_team(arguments)
        computer = parse_computer(arguments)
        if computer not in MapAPI.instance.query_team_computers(team.team_id):
            raise ValueError("Computer does not belong to team")

        task = asyncio.create_task(frame_pump(computer, send_message))
        log_handler = make_log_handler(send_message, asyncio.get_running_loop())
        computer.subscribe_logs(log_handler)
        return {
            "state": {
                "computer": computer,
                "team_id": team.team_id,
                "log_handler": log_handler,
                "task": task,
            },
            "messages": [
                {
                    "type": "log_history",
                    "messages": [m.decode(errors="replace") for m in computer.last_log_messages],
                }
            ],
        }

    async def message(self, content, auth, state):
        if isinstance(content, dict) and state:
            computer = state.get("computer")
            if computer is not None and content.get("type") == "key":
                row = content.get("row")
                data = content.get("data")
                if isinstance(row, int) and isinstance(data, int):
                    computer.set_key(row & 0xff, data & 0x1f)
            elif content.get("type") == "upload":
                team_id = state.get("team_id")
                if isinstance(team_id, int):
                    try:
                        status = handle_upload(team_id, "", content)
                    except Exception as exc:
                        status = status_message("error", str(exc))
                    return {
                        "state": state,
                        "messages": [status],
                    }
        return {
            "state": state,
            "messages": [],
        }

    async def close(self, auth, state):
        if not state:
            return
        computer = state.get("computer")
        if computer is not None:
            for row in KEYBOARD_ROWS:
                computer.set_key(row, 0x1f)
            log_handler = state.get("log_handler")
            if log_handler is not None:
                computer.unsubscribe_logs(log_handler)
        task = state.get("task")
        if task is not None:
            task.cancel()
            try:
                await task
            except asyncio.CancelledError:
                pass


session = ComputerSession()
