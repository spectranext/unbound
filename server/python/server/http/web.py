import os

from jinja2 import Environment, FileSystemLoader, select_autoescape

from .web_actions import get_action
from .web_auth import verify_web_token

templates_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "templates")
template_env = Environment(
    loader=FileSystemLoader(templates_dir),
    autoescape=select_autoescape(["html", "xml"]),
)


def web_error(status_code: int, message: str):
    return {
        "status_code": status_code,
        "content_type": "text/plain; charset=utf-8",
        "body": message,
    }


def render_template(name: str, context: dict):
    template = template_env.get_template(name)
    return template.render(**context)


def authenticate_web_request(server_hash: str, action_name: str, arguments: dict, content: dict | None = None):
    token = arguments.get("token")
    if token is None and content is not None:
        token = content.get("token")

    if not isinstance(token, str) or not token:
        raise ValueError("Missing token")

    payload = verify_web_token(token, server_hash, action_name)
    return token, payload


def resolve_web_action(server_hash: str, action_name: str, arguments: dict, content: dict | None = None):
    token, payload = authenticate_web_request(server_hash, action_name, arguments, content)
    action = get_action(action_name)
    if action is None:
        raise LookupError("Unknown web action")

    return action, {
        "server_hash": server_hash,
        "action": action_name,
        "token": token,
        "name": payload["name"],
        **{
            key: value
            for key, value in payload.items()
            if key not in ("sub", "server_hash", "action", "name", "iat", "exp")
        },
    }


async def handle_web_get(server_hash: str, action_name: str, arguments: dict):
    try:
        action, auth = resolve_web_action(server_hash, action_name, arguments)
    except ValueError as exc:
        return web_error(401, str(exc))
    except LookupError as exc:
        return web_error(404, "Unknown web action")

    result = await action.get(arguments, auth)
    return {
        "status_code": 200,
        "content_type": "text/html; charset=utf-8",
        "body": render_template(result["template"], result["context"]),
    }


async def handle_web_post(server_hash: str, action_name: str, arguments: dict, content: dict):
    try:
        action, auth = resolve_web_action(server_hash, action_name, arguments, content)
    except ValueError as exc:
        return web_error(401, str(exc))
    except LookupError as exc:
        return web_error(404, "Unknown web action")

    result = await action.post(arguments, content, auth)
    return {
        "status_code": 200,
        "content_type": "text/html; charset=utf-8",
        "body": render_template(result["template"], result["context"]),
    }


def get_session_handler(action):
    session_handler = getattr(action, "session", None)
    if session_handler is not None:
        return session_handler

    open_handler = getattr(action, "session_open", None)
    message_handler = getattr(action, "session_message", None)
    close_handler = getattr(action, "session_close", None)
    if open_handler is None and message_handler is None and close_handler is None:
        return None

    class LegacySessionHandler:
        async def open(self, arguments, auth, send_message, session_id):
            if open_handler is None:
                return None
            return await open_handler(arguments, auth)

        async def message(self, content, auth, state):
            if message_handler is None:
                return None
            return await message_handler(content, auth, state)

        async def close(self, auth, state):
            if close_handler is not None:
                await close_handler(auth, state)

    return LegacySessionHandler()


async def open_web_session(server_hash: str, action_name: str, arguments: dict, send_message, session_id: str):
    action, auth = resolve_web_action(server_hash, action_name, arguments)

    handler = get_session_handler(action)
    state = None
    messages = []
    if handler is not None:
        result = await handler.open(arguments, auth, send_message, session_id)
        if result:
            state = result.get("state")
            messages = result.get("messages", [])

    return {
        "action": action_name,
        "auth": auth,
        "state": state,
        "messages": messages,
        "send_message": send_message,
    }


async def message_web_session(session_data: dict, content):
    action = get_action(session_data["action"])
    if action is None:
        return []

    handler = get_session_handler(action)
    if handler is None:
        return []

    result = await handler.message(content, session_data["auth"], session_data.get("state"))
    if not result:
        return []

    session_data["state"] = result.get("state", session_data.get("state"))
    return result.get("messages", [])


async def close_web_session(session_data: dict):
    action = get_action(session_data["action"])
    if action is None:
        return

    handler = get_session_handler(action)
    if handler is not None:
        await handler.close(session_data["auth"], session_data.get("state"))
