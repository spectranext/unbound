import asyncio
import hashlib
import json
import os
import time
from urllib.parse import urlparse, urlunparse
from urllib.request import urlopen

import websockets

from .. api.map import MapAPI
from .web import close_web_session, handle_web_get, handle_web_post, message_web_session, open_web_session
from .web_auth import generate_web_token


def report_websocket_url(report_url: str) -> str:
    parsed = urlparse(report_url)

    if parsed.scheme in ("ws", "wss"):
        base = parsed
    elif parsed.scheme in ("http", "https"):
        base = parsed._replace(scheme="wss" if parsed.scheme == "https" else "ws")
    else:
        raise ValueError(f"Unsupported REPORT_URL scheme: {report_url}")

    path = base.path.rstrip("/")
    if not path.endswith("/rpc"):
        path = f"{path}/rpc" if path else "/rpc"

    return urlunparse(base._replace(path=path))


def report_web_url(report_url: str, server_hash: str, action: str, name: str, context: dict | None = None) -> str:
    parsed = urlparse(report_url)

    if parsed.scheme in ("ws", "wss"):
        scheme = "https" if parsed.scheme == "wss" else "http"
    elif parsed.scheme in ("http", "https"):
        scheme = parsed.scheme
    else:
        raise ValueError(f"Unsupported REPORT_URL scheme: {report_url}")

    path = parsed.path.rstrip("/")
    if path.endswith("/rpc"):
        path = path[:-4]
    path = f"{path}/web/{server_hash}/{action}" if path else f"/web/{server_hash}/{action}"
    token = generate_web_token(server_hash, action, name, context=context)
    return urlunparse(parsed._replace(
        scheme=scheme,
        path=path,
        params="",
        query=f"token={token}",
        fragment="",
    ))


def compute_server_hash(address: str, port: str) -> str:
    return hashlib.sha256(f"{address}:{port}".encode("utf-8")).hexdigest()


def get_report_address() -> str:
    address = os.environ.get("REPORT_ADDRESS", "").strip()
    if address:
        return address

    with urlopen("https://ifconfig.me/ip", timeout=5) as response:
        address = response.read().decode("utf-8").strip()

    if not address:
        raise RuntimeError("ifconfig.me returned an empty REPORT_ADDRESS")

    return address


def collect_server_stats():
    from .. team import TEAMS

    return {
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


def jsonrpc_error(code: int, message: str, request_id, data=None):
    error = {
        "code": code,
        "message": message,
    }
    if data is not None:
        error["data"] = data
    return {
        "jsonrpc": "2.0",
        "error": error,
        "id": request_id,
    }


class JsonRpcSession:
    def __init__(self, websocket):
        self.websocket = websocket
        self.next_id = 1
        self.pending = {}
        self.pending_lock = asyncio.Lock()
        self.send_lock = asyncio.Lock()
        self.web_sessions = {}

    async def handle_message(self, payload):
        if not isinstance(payload, dict) or payload.get("jsonrpc") != "2.0":
            raise RuntimeError(f"Invalid JSON-RPC message: {payload}")

        if "method" in payload:
            return await self.handle_request(payload)

        return await self.handle_response(payload)

    async def handle_response(self, payload):
        if "id" not in payload:
            raise RuntimeError(f"JSON-RPC response missing id: {payload}")

        request_id = payload["id"]
        future = self.pending.pop(request_id, None)
        if future is None:
            raise RuntimeError(f"Unexpected JSON-RPC response id: {request_id}")

        if "error" in payload:
            future.set_exception(RuntimeError(f"JSON-RPC error: {payload['error']}"))
            return

        if "result" not in payload:
            future.set_exception(RuntimeError(f"JSON-RPC response missing result: {payload}"))
            return

        future.set_result(payload["result"])

    async def handle_request(self, payload):
        method = payload.get("method")
        params = payload.get("params")
        request_id = payload.get("id")

        if not isinstance(method, str):
            if request_id is not None:
                await self.send(jsonrpc_error(-32600, "Invalid Request", request_id))
            return

        if params is None:
            params = {}

        if not isinstance(params, dict):
            if request_id is not None:
                await self.send(jsonrpc_error(-32602, "Invalid params", request_id))
            return

        if method == "web_get":
            result = await self.handle_web_get(params, request_id)
        elif method == "web_post":
            result = await self.handle_web_post(params, request_id)
        elif method == "session_open":
            result = await self.handle_session_open(params, request_id)
        elif method == "session_message":
            result = await self.handle_session_message(params, request_id)
        elif method == "session_close":
            result = await self.handle_session_close(params, request_id)
        else:
            result = jsonrpc_error(-32601, "Method not found", request_id)

        if request_id is None:
            return

        if isinstance(result, dict) and result.get("jsonrpc") == "2.0":
            await self.send(result)
            return

        await self.send({
            "jsonrpc": "2.0",
            "result": result,
            "id": request_id,
        })

    async def handle_web_get(self, params, request_id):
        action = params.get("action")
        server_hash = params.get("server_hash")
        arguments = params.get("arguments", {})

        if not isinstance(action, str) or not action:
            return jsonrpc_error(-32602, "Invalid action", request_id, {"http_status": 400})
        if not isinstance(server_hash, str) or not server_hash:
            return jsonrpc_error(-32602, "Invalid server hash", request_id, {"http_status": 400})
        if not isinstance(arguments, dict):
            return jsonrpc_error(-32602, "Invalid arguments", request_id, {"http_status": 400})

        return await handle_web_get(server_hash, action, arguments)

    async def handle_web_post(self, params, request_id):
        action = params.get("action")
        server_hash = params.get("server_hash")
        arguments = params.get("arguments", {})
        content = params.get("content", {})

        if not isinstance(action, str) or not action:
            return jsonrpc_error(-32602, "Invalid action", request_id, {"http_status": 400})
        if not isinstance(server_hash, str) or not server_hash:
            return jsonrpc_error(-32602, "Invalid server hash", request_id, {"http_status": 400})
        if not isinstance(arguments, dict):
            return jsonrpc_error(-32602, "Invalid arguments", request_id, {"http_status": 400})
        if not isinstance(content, dict):
            return jsonrpc_error(-32602, "Invalid content", request_id, {"http_status": 400})

        return await handle_web_post(server_hash, action, arguments, content)

    async def handle_session_open(self, params, request_id):
        session_id = params.get("session_id")
        action = params.get("action")
        server_hash = params.get("server_hash")
        arguments = params.get("arguments", {})

        if not isinstance(session_id, str) or not session_id:
            return jsonrpc_error(-32602, "Invalid session id", request_id, {"http_status": 400})
        if not isinstance(action, str) or not action:
            return jsonrpc_error(-32602, "Invalid action", request_id, {"http_status": 400})
        if not isinstance(server_hash, str) or not server_hash:
            return jsonrpc_error(-32602, "Invalid server hash", request_id, {"http_status": 400})
        if not isinstance(arguments, dict):
            return jsonrpc_error(-32602, "Invalid arguments", request_id, {"http_status": 400})

        async def emit_message(content):
            await self.notify("session_message", {
                "session_id": session_id,
                "content": content,
            })

        try:
            session_data = await open_web_session(server_hash, action, arguments, emit_message, session_id)
        except ValueError as exc:
            return jsonrpc_error(-32000, str(exc), request_id, {"http_status": 401})
        except LookupError as exc:
            return jsonrpc_error(-32004, str(exc), request_id, {"http_status": 404})

        self.web_sessions[session_id] = session_data

        for message in session_data.get("messages", []):
            await emit_message(message)

        return {"status": "opened"}

    async def handle_session_message(self, params, request_id):
        session_id = params.get("session_id")
        content = params.get("content")

        if not isinstance(session_id, str) or not session_id:
            return jsonrpc_error(-32602, "Invalid session id", request_id, {"http_status": 400})

        session_data = self.web_sessions.get(session_id)
        if session_data is None:
            return jsonrpc_error(-32004, "Session not found", request_id, {"http_status": 404})

        messages = await message_web_session(session_data, content)
        for message in messages:
            await self.notify("session_message", {
                "session_id": session_id,
                "content": message,
            })

        return {"status": "forwarded"}

    async def handle_session_close(self, params, request_id):
        session_id = params.get("session_id")

        if not isinstance(session_id, str) or not session_id:
            return jsonrpc_error(-32602, "Invalid session id", request_id, {"http_status": 400})

        session_data = self.web_sessions.pop(session_id, None)
        if session_data is not None:
            await close_web_session(session_data)

        return {"status": "closed"}

    async def request(self, method: str, params: dict):
        async with self.pending_lock:
            request_id = self.next_id
            self.next_id += 1

        loop = asyncio.get_running_loop()
        future = loop.create_future()
        self.pending[request_id] = future

        try:
            await self.send({
                "jsonrpc": "2.0",
                "id": request_id,
                "method": method,
                "params": params,
            })
        except Exception:
            self.pending.pop(request_id, None)
            raise

        return await future

    async def notify(self, method: str, params: dict):
        await self.send({
            "jsonrpc": "2.0",
            "method": method,
            "params": params,
        })

    async def send(self, payload: dict):
        async with self.send_lock:
            await self.websocket.send(json.dumps(payload))

    def close_pending(self, exc: Exception):
        for future in self.pending.values():
            if not future.done():
                future.set_exception(exc)
        self.pending.clear()

    async def close_all_web_sessions(self):
        session_ids = list(self.web_sessions.keys())
        for session_id in session_ids:
            session_data = self.web_sessions.pop(session_id, None)
            if session_data is None:
                continue
            await close_web_session(session_data)
            try:
                await self.notify("session_close", {"session_id": session_id})
            except Exception:
                pass


async def session_loop(rpc: JsonRpcSession):
    try:
        async for message in rpc.websocket:
            payload = json.loads(message)
            await rpc.handle_message(payload)
    except Exception as exc:
        rpc.close_pending(exc)
        await rpc.close_all_web_sessions()
        raise


async def reporter_loop(report_url, server_hash: str):
    name = os.environ["REPORT_NAME"]
    address = get_report_address()
    port = os.environ["REPORT_PORT"]
    icon = os.environ["REPORT_ICON"]
    icon_color = os.environ["REPORT_ICON_COLOR"]
    ws_url = report_websocket_url(report_url)
    admin_url = report_web_url(report_url, server_hash, "admin", "admin")

    while True:
        try:
            async with websockets.connect(ws_url) as websocket:
                MapAPI.instance.print(f"Reporter connected to {ws_url}")
                rpc = JsonRpcSession(websocket)
                reader_task = asyncio.create_task(session_loop(rpc))
                await rpc.request("register", {"server_hash": server_hash})
                MapAPI.instance.print(f"Reporter registered hash {server_hash}")
                MapAPI.instance.print(f"Admin portal: {admin_url}")

                try:
                    while True:
                        num_players = len(MapAPI.instance.query_clients())
                        title = "{0} | {1} online".format(name, num_players)

                        await rpc.notify("update_status", {
                            "title": title,
                            "address": address,
                            "port": port,
                            "icon": icon,
                            "color": icon_color,
                            "stats": collect_server_stats(),
                        })
                        MapAPI.instance.print(f"Reported status to {ws_url}: {title}")
                        await asyncio.sleep(30)
                finally:
                    reader_task.cancel()
                    try:
                        await reader_task
                    except asyncio.CancelledError:
                        pass
                    except Exception as exc:
                        MapAPI.instance.print(f"Reporter session closed with error: {exc}")
        except Exception as exc:
            MapAPI.instance.print(f"Reporter connection to {ws_url} lost: {exc}")
            await asyncio.sleep(5)


def start_reporter_thread(report_url):
    time.sleep(1)
    address = get_report_address()
    port = os.environ["REPORT_PORT"]
    server_hash = os.environ.get("REPORT_SERVER_HASH", compute_server_hash(address, port))
    MapAPI.instance.print(f"Reporter server hash {server_hash}")
    asyncio.run(reporter_loop(report_url, server_hash))


def start_reporter():
    report_url = os.environ.get("REPORT_URL", None)
    if report_url is None:
        return

    MapAPI.instance.print(f"Reporting status to {report_url}")

    import threading

    reporter_thread = threading.Thread(target=start_reporter_thread, args=[report_url])
    reporter_thread.daemon = True
    reporter_thread.start()
