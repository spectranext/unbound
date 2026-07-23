import base64
import os
import shutil
from pathlib import Path

from ...api.map import MapAPI
from ...team import TEAMS
from ..web_auth import generate_web_token

TEMPLATE_NAME = "web/team.html"
SERVER_ROOT = Path(__file__).resolve().parents[4]
TEAM_FILES_ROOT = SERVER_ROOT / "runtime" / "xfs"


def find_team(team_id: int):
    for team in TEAMS:
        if team.team_id == team_id:
            return team
    return None


def parse_team(arguments: dict, auth: dict | None = None):
    raw_team_id = arguments.get("team_id")
    if isinstance(raw_team_id, str) and raw_team_id.isdigit():
        team_id = int(raw_team_id)
    elif isinstance(raw_team_id, int):
        team_id = raw_team_id
    else:
        raise ValueError("Invalid team id")

    team = find_team(team_id)
    if team is None:
        raise ValueError("Unknown team")
    if auth is not None and auth.get("player_user_id") and auth.get("team_id") != team.team_id:
        raise ValueError("Team is outside player context")
    return team


def team_root(team_id: int) -> Path:
    root = TEAM_FILES_ROOT / f"team_{team_id}"
    root.mkdir(parents=True, exist_ok=True)
    return root.resolve()


def normalize_segment(name: str) -> str:
    if not isinstance(name, str):
        raise ValueError("Invalid name")
    name = name.strip()
    if not name or name in (".", "..") or "/" in name or "\\" in name:
        raise ValueError("Invalid name")
    return name


def resolve_team_path(team_id: int, relative_path: str = "") -> Path:
    root = team_root(team_id)
    relative_path = (relative_path or "").strip().replace("\\", "/")
    target = (root / relative_path).resolve()
    if target != root and root not in target.parents:
        raise ValueError("Invalid path")
    return target


def rel_path(root: Path, path: Path) -> str:
    if path == root:
        return ""
    return path.relative_to(root).as_posix()


def list_directory(team_id: int, current_path: str):
    root = team_root(team_id)
    current_dir = resolve_team_path(team_id, current_path)
    if not current_dir.exists():
        current_dir.mkdir(parents=True, exist_ok=True)
    if not current_dir.is_dir():
        raise ValueError("Not a directory")

    breadcrumbs = [{"name": "root", "path": ""}]
    running = root
    for part in rel_path(root, current_dir).split("/"):
        if not part:
            continue
        running = running / part
        breadcrumbs.append({"name": part, "path": rel_path(root, running)})

    entries = []
    for entry in sorted(current_dir.iterdir(), key=lambda p: (not p.is_dir(), p.name.lower())):
        entry_rel = rel_path(root, entry)
        entries.append({
            "name": entry.name,
            "path": entry_rel,
            "kind": "dir" if entry.is_dir() else "file",
            "size": None if entry.is_dir() else entry.stat().st_size,
        })

    parent = rel_path(root, current_dir.parent) if current_dir != root else None
    return {
        "type": "state",
        "current_path": rel_path(root, current_dir),
        "parent_path": parent,
        "entries": entries,
        "breadcrumbs": breadcrumbs,
    }


def status_message(kind: str, text: str):
    return {"type": "status", "kind": kind, "text": text}


def download_message(team_id: int, path: str):
    file_path = resolve_team_path(team_id, path)
    if not file_path.exists() or not file_path.is_file():
        raise ValueError("File not found")

    return {
        "type": "download",
        "name": file_path.name,
        "content_base64": base64.b64encode(file_path.read_bytes()).decode("ascii"),
    }


def handle_upload(team_id: int, current_path: str, content: dict):
    current_dir = resolve_team_path(team_id, current_path)
    if not current_dir.exists():
        current_dir.mkdir(parents=True, exist_ok=True)
    if not current_dir.is_dir():
        raise ValueError("Not a directory")

    file_name = normalize_segment(content.get("name", ""))
    payload = content.get("content_base64")
    if not isinstance(payload, str) or not payload:
        raise ValueError("Missing file content")

    target = current_dir / file_name
    target.write_bytes(base64.b64decode(payload))
    return status_message("success", f"Uploaded {file_name}")


def handle_rename(team_id: int, content: dict):
    source = resolve_team_path(team_id, content.get("path", ""))
    if not source.exists():
        raise ValueError("Path not found")

    target = source.with_name(normalize_segment(content.get("new_name", "")))
    target = target.resolve()
    root = team_root(team_id)
    if target != root and root not in target.parents:
        raise ValueError("Invalid path")

    source.rename(target)
    return status_message("success", f"Renamed to {target.name}")


def handle_delete(team_id: int, content: dict):
    target = resolve_team_path(team_id, content.get("path", ""))
    if not target.exists():
        raise ValueError("Path not found")

    if target.is_dir():
        shutil.rmtree(target)
    else:
        target.unlink()
    return status_message("success", f"Deleted {target.name}")


def handle_mkdir(team_id: int, current_path: str, content: dict):
    current_dir = resolve_team_path(team_id, current_path)
    if not current_dir.exists():
        current_dir.mkdir(parents=True, exist_ok=True)
    if not current_dir.is_dir():
        raise ValueError("Not a directory")

    folder_name = normalize_segment(content.get("name", ""))
    (current_dir / folder_name).mkdir(parents=False, exist_ok=False)
    return status_message("success", f"Created folder {folder_name}")


def build_context(auth, team):
    computer_token = generate_web_token(auth["server_hash"], "computer", auth["name"])
    chat_context = {
        key: value
        for key, value in auth.items()
        if key not in ("server_hash", "action", "token", "name")
    }
    chat_token = generate_web_token(
        auth["server_hash"],
        "chat",
        auth["name"],
        context=chat_context,
    )
    computers = []
    for computer in sorted(MapAPI.instance.query_team_computers(team.team_id),
                           key=lambda c: c.get_hostname().decode(errors="replace").lower()):
        computer_hash = computer.get_hash().decode()
        computers.append({
            "name": computer.get_hostname().decode(errors="replace"),
            "powered_on": computer.is_powered_on(),
            "manage_url": (
                f"/web/{auth['server_hash']}/computer"
                f"?token={computer_token}"
                f"&team_id={team.team_id}"
                f"&computer_hash={computer_hash}"
            ),
        })

    return {
        "auth": auth,
        "team": team,
        "credits": team.credits,
        "computers": computers,
        "chat_url": f"/web/{auth['server_hash']}/chat?token={chat_token}",
        "server_title": os.environ.get("REPORT_NAME", "Server"),
    }


async def get(arguments, auth):
    team = parse_team(arguments, auth)
    return {
        "template": TEMPLATE_NAME,
        "context": build_context(auth, team),
    }


async def post(arguments, content, auth):
    return await get(arguments, auth)


class TeamSession:
    async def open(self, arguments, auth, send_message, session_id):
        team = parse_team(arguments, auth)
        state = {
            "team_id": team.team_id,
            "current_path": "",
        }
        return {
            "state": state,
            "messages": [list_directory(team.team_id, "")],
        }

    async def message(self, content, auth, state):
        if state is None:
            return {"state": state, "messages": [status_message("error", "Missing session state")]}
        if not isinstance(content, dict):
            return {"state": state, "messages": []}

        team_id = state["team_id"]
        current_path = state.get("current_path", "")
        message_type = content.get("type")
        messages = []

        try:
            if message_type == "refresh":
                next_path = content.get("path", current_path)
                directory_state = list_directory(team_id, next_path)
                state["current_path"] = directory_state["current_path"]
                messages.append(directory_state)
            elif message_type == "upload":
                messages.append(handle_upload(team_id, current_path, content))
                directory_state = list_directory(team_id, current_path)
                state["current_path"] = directory_state["current_path"]
                messages.append(directory_state)
            elif message_type == "rename":
                messages.append(handle_rename(team_id, content))
                directory_state = list_directory(team_id, current_path)
                state["current_path"] = directory_state["current_path"]
                messages.append(directory_state)
            elif message_type == "delete":
                messages.append(handle_delete(team_id, content))
                directory_state = list_directory(team_id, current_path)
                state["current_path"] = directory_state["current_path"]
                messages.append(directory_state)
            elif message_type == "mkdir":
                messages.append(handle_mkdir(team_id, current_path, content))
                directory_state = list_directory(team_id, current_path)
                state["current_path"] = directory_state["current_path"]
                messages.append(directory_state)
            elif message_type == "download":
                messages.append(download_message(team_id, content.get("path", "")))
            else:
                messages.append(status_message("error", "Unknown action"))
        except Exception as exc:
            messages.append(status_message("error", str(exc)))
            directory_state = list_directory(team_id, current_path)
            state["current_path"] = directory_state["current_path"]
            messages.append(directory_state)

        return {
            "state": state,
            "messages": messages,
        }

    async def close(self, auth, state):
        return None


session = TeamSession()
