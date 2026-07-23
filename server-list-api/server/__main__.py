import asyncio
import json
import os
import struct
import sys

from fastapi import FastAPI, Request, WebSocket, WebSocketDisconnect
from fastapi.responses import Response
from fastapi.templating import Jinja2Templates
import redis
from redis import asyncio as aioredis

from .rpc import RpcResponseError, RpcServer

app = FastAPI()

REDIS_HOST = os.environ.get("REDIS_HOST", "localhost")
STATUSES = os.environ.get("STATUSES", "./statuses.bin")
templates_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "templates")
templates = Jinja2Templates(directory=templates_dir)

r = redis.Redis(host=REDIS_HOST, port=6379, db=0)
rpc_server = RpcServer(r)
app.websocket("/rpc")(rpc_server.handle_websocket)


def error_response(request: Request, status_code: int, title: str, message: str):
    return templates.TemplateResponse(
        request=request,
        name="error.html",
        context={
            "status_code": status_code,
            "title": title,
            "message": message,
        },
        status_code=status_code,
    )


def error_title(status_code: int) -> str:
    if status_code == 400:
        return "Bad Request"
    if status_code == 401:
        return "Unauthorized"
    if status_code == 403:
        return "Forbidden"
    if status_code == 404:
        return "Not Found"
    if status_code == 408:
        return "Request Timeout"
    if status_code == 502:
        return "Proxy Error"
    if status_code == 504:
        return "Gateway Timeout"
    if status_code >= 500:
        return "Server Error"
    if status_code >= 400:
        return "Request Error"
    return "Response Error"


@app.api_route("/web/{server_hash}/{action:path}", methods=["GET", "POST"])
async def web_portal(request: Request, server_hash: str, action: str):
    session = rpc_server.get_session(server_hash)
    if session is None:
        return error_response(request, 404, "Server Not Found", "Server session not found.")

    query_arguments = dict(request.query_params)

    rpc_method = "web_get"
    rpc_params = {
        "server_hash": server_hash,
        "action": action,
        "arguments": query_arguments,
    }

    if request.method == "POST":
        rpc_method = "web_post"
        try:
            form = await request.form()
        except Exception:
            form = {}
        rpc_params["content"] = {key: value for key, value in form.items()}

    try:
        result = await rpc_server.request(session, rpc_method, rpc_params)
    except RpcResponseError as exc:
        error = exc.error
        data = error.get("data") if isinstance(error, dict) else None
        http_status = 502
        if isinstance(data, dict):
            http_status = int(data.get("http_status", http_status))
        return error_response(
            request,
            http_status,
            "Server Request Failed",
            error.get("message", "Upstream RPC error"),
        )
    except TimeoutError:
        return error_response(request, 504, "Request Timed Out", "Server web request timed out.")
    except Exception as exc:
        return error_response(
            request,
            502,
            "Proxy Error",
            f"Failed to proxy server request: {exc}",
        )

    if not isinstance(result, dict):
        return error_response(request, 502, "Invalid Server Response", "Invalid server response.")

    status_code = result.get("status_code", 200)
    content_type = result.get("content_type", "text/html; charset=utf-8")
    body = result.get("body", "")

    if not isinstance(body, str):
        return error_response(
            request,
            502,
            "Invalid Server Response",
            "Invalid server response body.",
        )

    if int(status_code) >= 400:
        return error_response(request, int(status_code), error_title(int(status_code)), body)

    response = Response(content=body, status_code=int(status_code))
    response.headers["content-type"] = content_type
    return response


@app.websocket("/web/{server_hash}/{action:path}")
async def web_portal_websocket(websocket: WebSocket, server_hash: str, action: str):
    session = rpc_server.get_session(server_hash)
    await websocket.accept()

    if session is None:
        await websocket.close(code=4404, reason="Server session not found")
        return

    query_arguments = dict(websocket.query_params)

    try:
        proxy_session = await rpc_server.open_web_session(session, action, websocket, query_arguments)
    except Exception as exc:
        await websocket.close(code=1011, reason=f"Failed to open session: {exc}")
        return

    try:
        while True:
            payload = await websocket.receive_json()
            await rpc_server.notify(session, "session_message", {
                "session_id": proxy_session.session_id,
                "content": payload,
            })
    except WebSocketDisconnect:
        pass
    except Exception:
        pass
    finally:
        try:
            await rpc_server.notify(session, "session_close", {
                "session_id": proxy_session.session_id,
            })
        except Exception:
            pass
        await rpc_server.close_web_session(proxy_session.session_id, code=1000, reason="Closed by client")


async def generate_binary_file():
    redis_client = await aioredis.from_url(f"redis://{REDIS_HOST}")
    while True:
        await asyncio.sleep(5)
        try:
            keys = await redis_client.keys("*")
            with open(STATUSES, "wb") as f:
                for key in keys:
                    try:
                        data = await redis_client.get(key)
                        if not data:
                            continue
                        status = json.loads(data.decode("utf-8"))
                        icon = status["icon"].encode("latin1")

                        record = struct.pack(
                            "64s64sH8sB61s",
                            status["title"].encode("utf-8")[:64].ljust(64, b"\x00"),
                            status["address"].encode("utf-8")[:64].ljust(64, b"\x00"),
                            status["port"],
                            icon[:8],
                            status["color"],
                            b"\x00" * 61,
                        )
                        f.write(record)
                    except Exception as exc:
                        print(
                            f"Failed to process status for key {key!r}: {exc}",
                            file=sys.stderr,
                        )
        except Exception as exc:
            print(f"Failed to generate binary file: {exc}", file=sys.stderr)

    await redis_client.close()


async def main():
    asyncio.create_task(generate_binary_file())

    from hypercorn.asyncio import serve
    from hypercorn.config import Config

    config = Config()
    config.bind = ["0.0.0.0:5000"]
    await serve(app, config)


if __name__ == "__main__":
    asyncio.run(main())
