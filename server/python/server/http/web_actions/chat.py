from ..web_chat import CHAT_HUB, normalize_chat_text, publish_chat_message, register_chat_loop

TEMPLATE_NAME = "web/chat.html"


def build_context(auth):
    return {
        "auth": auth,
        "display_name": auth["name"],
    }


async def get(arguments, auth):
    return {
        "template": TEMPLATE_NAME,
        "context": build_context(auth),
    }


async def post(arguments, content, auth):
    return await get(arguments, auth)


class ChatSession:
    async def open(self, arguments, auth, send_message, session_id):
        register_chat_loop()
        history = await CHAT_HUB.subscribe(session_id, send_message)
        return {
            "state": {
                "session_id": session_id,
            },
            "messages": history,
        }

    async def message(self, content, auth, state):
        if not isinstance(content, dict):
            return {"state": state, "messages": []}

        if content.get("type") != "chat_send":
            return {"state": state, "messages": []}

        text = normalize_chat_text(content.get("text"))
        if text is None:
            return {"state": state, "messages": []}

        await publish_chat_message(auth["name"], text)
        return {"state": state, "messages": []}

    async def close(self, auth, state):
        if state is None:
            return
        session_id = state.get("session_id")
        if isinstance(session_id, str) and session_id:
            await CHAT_HUB.unsubscribe(session_id)


session = ChatSession()
