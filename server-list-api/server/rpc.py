import asyncio
import inspect
import sys
import time
from dataclasses import dataclass, field
from typing import Optional

from fastapi import WebSocket, WebSocketDisconnect

from .rpc_calls import RPC_CALLS


@dataclass
class ServerSession:
    websocket: WebSocket
    client_label: str
    server_hash: Optional[str] = None
    status_key: Optional[str] = None
    last_status: Optional[dict] = None
    last_seen: float = field(default_factory=time.time)
    next_id: int = 1
    pending: dict = field(default_factory=dict)
    send_lock: asyncio.Lock = field(default_factory=asyncio.Lock)
    id_lock: asyncio.Lock = field(default_factory=asyncio.Lock)

    def register(self, server_hash: str):
        self.server_hash = server_hash
        self.last_seen = time.time()

    def touch(self):
        self.last_seen = time.time()

    async def allocate_id(self) -> int:
        async with self.id_lock:
            request_id = self.next_id
            self.next_id += 1
            return request_id


@dataclass
class WebProxySession:
    session_id: str
    server_hash: str
    action: str
    websocket: WebSocket


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


def is_jsonrpc_response(payload):
    return (
        isinstance(payload, dict)
        and payload.get("jsonrpc") == "2.0"
        and "method" not in payload
        and "id" in payload
        and ("result" in payload or "error" in payload)
    )


def client_label_for(websocket: WebSocket) -> str:
    forwarded_for = websocket.headers.get("x-forwarded-for")
    forwarded_host = None
    if forwarded_for:
        forwarded_host = forwarded_for.split(",", 1)[0].strip() or None

    client = websocket.client
    if forwarded_host and client:
        return f"{forwarded_host} via {client.host}:{client.port}"
    if forwarded_host:
        return forwarded_host
    if client:
        return f"{client.host}:{client.port}"
    return "<unknown>"


class RpcResponseError(Exception):
    def __init__(self, error):
        super().__init__(error.get("message", "JSON-RPC error"))
        self.error = error


class RpcServer:
    def __init__(self, redis_client):
        self.redis_client = redis_client
        self.server_sessions = {}
        self.web_sessions = {}
        self.rpc_calls = dict(RPC_CALLS)
        self.next_web_session_id = 1
        self.web_session_lock = asyncio.Lock()

    @staticmethod
    def jsonrpc_error(code: int, message: str, request_id, data=None):
        return jsonrpc_error(code, message, request_id, data)

    def get_session(self, server_hash: str) -> Optional[ServerSession]:
        return self.server_sessions.get(server_hash)

    def register_session(self, session: ServerSession, server_hash):
        if not isinstance(server_hash, str) or not server_hash:
            raise ValueError("Invalid server hash")

        existing = self.server_sessions.get(server_hash)
        if existing is not None and existing is not session:
            print(
                f"Replacing server session for hash {server_hash}: {existing.client_label} -> {session.client_label}",
                file=sys.stderr,
            )

        session.register(server_hash)
        self.server_sessions[server_hash] = session
        print(f"Registered server session {server_hash} from {session.client_label}", file=sys.stderr)

    async def unregister_session(self, session: ServerSession):
        server_hash = session.server_hash
        if session.server_hash and self.server_sessions.get(session.server_hash) is session:
            self.server_sessions.pop(session.server_hash, None)
            print(
                f"Unregistered server session {session.server_hash} from {session.client_label}",
                file=sys.stderr,
            )

        for future in list(session.pending.values()):
            if not future.done():
                future.set_exception(RuntimeError("Server session disconnected"))
        session.pending.clear()

        if server_hash:
            await self.close_server_web_sessions(server_hash, code=1011, reason="Server disconnected")

    async def request(self, session: ServerSession, method: str, params: dict, timeout: float = 15.0):
        request_id = await session.allocate_id()
        loop = asyncio.get_running_loop()
        future = loop.create_future()
        session.pending[request_id] = future

        try:
            async with session.send_lock:
                await session.websocket.send_json({
                    "jsonrpc": "2.0",
                    "id": request_id,
                    "method": method,
                    "params": params,
                })
        except Exception:
            session.pending.pop(request_id, None)
            raise

        try:
            return await asyncio.wait_for(future, timeout=timeout)
        finally:
            session.pending.pop(request_id, None)

    async def notify(self, session: ServerSession, method: str, params: dict):
        async with session.send_lock:
            await session.websocket.send_json({
                "jsonrpc": "2.0",
                "method": method,
                "params": params,
            })

    async def allocate_web_session_id(self) -> str:
        async with self.web_session_lock:
            session_id = str(self.next_web_session_id)
            self.next_web_session_id += 1
            return session_id

    async def open_web_session(self, session: ServerSession, action: str, websocket: WebSocket, arguments: dict):
        session_id = await self.allocate_web_session_id()
        proxy_session = WebProxySession(
            session_id=session_id,
            server_hash=session.server_hash,
            action=action,
            websocket=websocket,
        )
        self.web_sessions[session_id] = proxy_session

        try:
            await self.request(session, "session_open", {
                "session_id": session_id,
                "server_hash": session.server_hash,
                "action": action,
                "arguments": arguments,
            }, timeout=10.0)
        except Exception:
            self.web_sessions.pop(session_id, None)
            raise

        return proxy_session

    def get_web_session(self, session_id: str) -> Optional[WebProxySession]:
        return self.web_sessions.get(session_id)

    async def close_web_session(self, session_id: str, code: int = 1000, reason: str = ""):
        proxy_session = self.web_sessions.pop(session_id, None)
        if proxy_session is None:
            return

        try:
            await proxy_session.websocket.close(code=code, reason=reason)
        except Exception:
            pass

    async def close_server_web_sessions(self, server_hash: str, code: int = 1000, reason: str = ""):
        session_ids = [
            session_id
            for session_id, proxy_session in self.web_sessions.items()
            if proxy_session.server_hash == server_hash
        ]
        for session_id in session_ids:
            await self.close_web_session(session_id, code=code, reason=reason)

    async def resolve_response(self, session: ServerSession, payload):
        request_id = payload.get("id")
        future = session.pending.get(request_id)
        if future is None:
            print(
                f"Unexpected JSON-RPC response from {session.client_label}: {payload}",
                file=sys.stderr,
            )
            return

        if "error" in payload:
            future.set_exception(RpcResponseError(payload["error"]))
            return

        future.set_result(payload.get("result"))

    async def handle_rpc_request(self, session: ServerSession, payload):
        if not isinstance(payload, dict):
            return jsonrpc_error(-32600, "Invalid Request", None)

        request_id = payload.get("id")

        if payload.get("jsonrpc") != "2.0":
            return jsonrpc_error(-32600, "Invalid Request", request_id)

        method = payload.get("method")
        params = payload.get("params")

        if not isinstance(method, str):
            return jsonrpc_error(-32600, "Invalid Request", request_id)

        if params is None:
            params = {}

        if not isinstance(params, dict):
            return jsonrpc_error(-32602, "Invalid params", request_id)

        if method == "register":
            try:
                self.register_session(session, params.get("server_hash"))
            except ValueError as exc:
                return jsonrpc_error(-32602, str(exc), request_id)

            if request_id is None:
                return None

            return {
                "jsonrpc": "2.0",
                "result": {"status": "registered", "server_hash": session.server_hash},
                "id": request_id,
            }

        rpc_call = self.rpc_calls.get(method)
        if rpc_call is not None:
            try:
                result = rpc_call(self, session, params, request_id)
                if inspect.isawaitable(result):
                    return await result
                return result
            except Exception as exc:
                print(f"Failed to handle RPC method {method}: {exc}", file=sys.stderr)
                return jsonrpc_error(-32000, "Internal error", request_id)

        return jsonrpc_error(-32601, "Method not found", request_id)

    async def handle_websocket(self, websocket: WebSocket):
        client_label = client_label_for(websocket)
        print(f"RPC client connected: {client_label}", file=sys.stderr)
        await websocket.accept()
        session = ServerSession(websocket=websocket, client_label=client_label)

        while True:
            try:
                payload = await websocket.receive_json()
            except WebSocketDisconnect as exc:
                print(
                    f"RPC client disconnected: {client_label} code={exc.code}",
                    file=sys.stderr,
                )
                await self.unregister_session(session)
                break
            except Exception as exc:
                print(f"RPC parse error from {client_label}: {exc}", file=sys.stderr)
                await websocket.send_json(jsonrpc_error(-32700, "Parse error", None))
                continue

            if is_jsonrpc_response(payload):
                await self.resolve_response(session, payload)
                continue

            response = await self.handle_rpc_request(session, payload)
            if response is not None:
                await websocket.send_json(response)
