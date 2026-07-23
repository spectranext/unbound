import os

from ...api.map import MapAPI
from ...team import TEAMS
from ..web_auth import generate_web_token
from .player import decode_bytes, player_url

TEMPLATE_NAME = "web/admin.html"


def build_context(auth):
    chat_token = generate_web_token(auth["server_hash"], "chat", auth["name"])
    return {
        "server_title": os.environ.get("REPORT_NAME", "Server"),
        "auth": auth,
        "time_of_day": "{0:02}:{1:02}".format(
            MapAPI.instance.get_time_hours(),
            MapAPI.instance.get_time_minutes(),
        ),
        "teams": [
            {
                "team_id": team.team_id,
                "name": team.name,
                "credits": team.credits,
                "manage_url": (
                    f"/web/{auth['server_hash']}/team"
                    f"?token={generate_web_token(auth['server_hash'], 'team', auth['name'])}"
                    f"&team_id={team.team_id}"
                ),
            }
            for team in TEAMS
        ],
        "players": [
            {
                "name": decode_bytes(client.get_name()),
                "user_id": decode_bytes(client.get_user_id()),
                "team_name": client.get_team().name if client.get_team() else "",
                "see_url": player_url(auth, client),
            }
            for client in MapAPI.instance.query_clients()
        ],
        "chat_url": f"/web/{auth['server_hash']}/chat?token={chat_token}",
    }


def build_stats_message():
    context = {
        "server_title": os.environ.get("REPORT_NAME", "Server"),
        "time_of_day": "{0:02}:{1:02}".format(
            MapAPI.instance.get_time_hours(),
            MapAPI.instance.get_time_minutes(),
        ),
        "teams": [
            {
                "name": team.name,
                "credits": team.credits,
            }
            for team in TEAMS
        ],
    }
    return {
        "type": "stats",
        "server_title": context["server_title"],
        "time_of_day": context["time_of_day"],
        "teams": context["teams"],
    }


async def get(arguments, auth):
    return {
        "template": TEMPLATE_NAME,
        "context": build_context(auth),
    }


async def post(arguments, content, auth):
    return await get(arguments, auth)


class AdminSession:
    async def open(self, arguments, auth, send_message, session_id):
        return {
            "messages": [build_stats_message()],
        }

    async def message(self, content, auth, state):
        if isinstance(content, dict) and content.get("type") == "refresh":
            return {
                "state": state,
                "messages": [build_stats_message()],
            }

        return {
            "state": state,
            "messages": [],
        }

    async def close(self, auth, state):
        return None


session = AdminSession()
