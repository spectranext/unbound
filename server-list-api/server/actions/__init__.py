from . import admin


ACTIONS = {
    "admin": admin,
}


def get_action(action_name: str):
    return ACTIONS.get(action_name)
