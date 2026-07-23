import os

from ...api.client import ClientAPI
from ...api.map import MapAPI
from ..web_auth import generate_web_token

TEMPLATE_NAME = "web/player.html"


def decode_bytes(value: bytes) -> str:
    return value.decode("utf-8", errors="replace")


def player_context(client: ClientAPI, include_team: bool = False) -> dict:
    context = {
        "player_user_id": decode_bytes(client.get_user_id()),
    }
    team = client.get_team()
    if include_team and team is not None:
        context["team_id"] = team.team_id
    return context


def generate_player_web_token(server_hash: str, action: str, client: ClientAPI, include_team: bool = False) -> str:
    return generate_web_token(
        server_hash,
        action,
        decode_bytes(client.get_name()),
        context=player_context(client, include_team=include_team),
    )


def find_player(user_id: str):
    for client in MapAPI.instance.query_clients():
        if decode_bytes(client.get_user_id()) == user_id:
            return client
    return None


def auth_player(auth):
    user_id = auth.get("player_user_id")
    if not isinstance(user_id, str) or not user_id:
        raise ValueError("Missing player context")

    client = find_player(user_id)
    if client is None:
        raise ValueError("Player is no longer connected")
    return client


def player_url(auth, client: ClientAPI) -> str:
    token = generate_player_web_token(auth["server_hash"], "player", client)
    return f"/web/{auth['server_hash']}/player?token={token}"


def build_context(auth, client: ClientAPI):
    team = client.get_team()
    chat_token = generate_player_web_token(auth["server_hash"], "chat", client)
    team_token = generate_player_web_token(auth["server_hash"], "team", client, include_team=True)

    return {
        "server_title": os.environ.get("REPORT_NAME", "Server"),
        "auth": auth,
        "player": {
            "name": decode_bytes(client.get_name()),
            "user_id": decode_bytes(client.get_user_id()),
            "team_name": team.name if team else None,
            "team_url": (
                f"/web/{auth['server_hash']}/team"
                f"?token={team_token}"
                f"&team_id={team.team_id}"
            ) if team else None,
            "chat_url": f"/web/{auth['server_hash']}/chat?token={chat_token}",
        },
    }


async def get(arguments, auth):
    client = auth_player(auth)
    return {
        "template": TEMPLATE_NAME,
        "context": build_context(auth, client),
    }


async def post(arguments, content, auth):
    return await get(arguments, auth)
