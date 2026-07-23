TEMPLATE_NAME = "actions/admin.html"


def build_panels(session):
    stats = session.stats or {}
    time_of_day = stats.get("time_of_day", "--:--")
    teams = stats.get("teams", [])

    return [
        {
            "id": "server-stats",
            "title": "Server Stats",
            "time_of_day": time_of_day,
            "teams": teams,
        }
    ]


async def get(request, session, auth_params):
    return {
        "template": TEMPLATE_NAME,
        "context": {
            "request": request,
            "session": session,
            "auth_params": auth_params,
            "action_name": "admin",
            "panels": build_panels(session),
        },
    }


async def post(request, session, auth_params):
    return await get(request, session, auth_params)
