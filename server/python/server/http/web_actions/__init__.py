from . import admin, chat, computer, player, team


ACTIONS = {
    "admin": admin,
    "chat": chat,
    "computer": computer,
    "player": player,
    "team": team,
}


def get_action(action_name: str):
    return ACTIONS.get(action_name)
