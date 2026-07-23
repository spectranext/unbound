import binascii
import json
import sys
import time


def handle_update_status(rpc_server, session, params, request_id):
    if session.server_hash is None:
        return rpc_server.jsonrpc_error(-32001, "Session is not registered", request_id)

    title = params.get("title")
    address = params.get("address")
    port = params.get("port")
    icon_hex = params.get("icon")
    color = params.get("color")
    stats = params.get("stats")

    if title is None or address is None or port is None or icon_hex is None or color is None:
        return rpc_server.jsonrpc_error(-32602, "Missing required fields", request_id)

    if not isinstance(title, str) or not isinstance(address, str):
        return rpc_server.jsonrpc_error(-32602, "Invalid data types", request_id)

    try:
        port = int(port)
        color = int(color)
    except (TypeError, ValueError):
        return rpc_server.jsonrpc_error(-32602, "Invalid data types", request_id)

    try:
        icon = binascii.unhexlify(icon_hex)
    except (binascii.Error, ValueError):
        return rpc_server.jsonrpc_error(-32602, "Invalid icon hex value", request_id)

    if stats is not None and not isinstance(stats, dict):
        return rpc_server.jsonrpc_error(-32602, "Invalid stats type", request_id)

    print(f"Report <{title}>: {address} {port}", file=sys.stderr)

    key = f"{address}:{port}"
    session.status_key = key
    session.stats = stats
    session.last_status = {
        "title": title,
        "address": address,
        "port": port,
        "color": color,
    }
    session.touch()

    status = {
        "server_hash": session.server_hash,
        "title": title,
        "address": address,
        "port": port,
        "icon": icon.decode("latin1"),
        "color": color,
        "last_updated": time.time(),
    }
    rpc_server.redis_client.set(key, json.dumps(status), ex=300)

    if request_id is None:
        return None

    return {"jsonrpc": "2.0", "result": {"status": "updated"}, "id": request_id}


async def handle_session_message(rpc_server, session, params, request_id):
    session_id = params.get("session_id")
    content = params.get("content")

    if not isinstance(session_id, str) or not session_id:
        return rpc_server.jsonrpc_error(-32602, "Invalid session id", request_id)
    if content is None:
        return rpc_server.jsonrpc_error(-32602, "Missing session content", request_id)

    proxy_session = rpc_server.get_web_session(session_id)
    if proxy_session is None:
        return rpc_server.jsonrpc_error(-32004, "Web session not found", request_id)

    await proxy_session.websocket.send_json(content)

    if request_id is None:
        return None

    return {"jsonrpc": "2.0", "result": {"status": "forwarded"}, "id": request_id}


async def handle_session_close(rpc_server, session, params, request_id):
    session_id = params.get("session_id")

    if not isinstance(session_id, str) or not session_id:
        return rpc_server.jsonrpc_error(-32602, "Invalid session id", request_id)

    await rpc_server.close_web_session(session_id, code=1000, reason="Closed by server")

    if request_id is None:
        return None

    return {"jsonrpc": "2.0", "result": {"status": "closed"}, "id": request_id}


RPC_CALLS = {
    "update_status": handle_update_status,
    "session_message": handle_session_message,
    "session_close": handle_session_close,
}
