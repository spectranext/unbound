from typing import Optional

import threading
import uvicorn
import asyncio
import os

from fastapi import FastAPI, Request, UploadFile, status, WebSocket, WebSocketDisconnect
from fastapi.responses import HTMLResponse, RedirectResponse
from fastapi.staticfiles import StaticFiles
from fastapi.templating import Jinja2Templates

from .. api.map import MapAPI
from .rpc import start_reporter

static_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "static")
templates_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "templates")

app = FastAPI()
app.mount("/static", StaticFiles(directory=static_dir), name="static")
templates = Jinja2Templates(directory=templates_dir)


@app.get("/")
async def read_root():
    return {"message": "Hello, World"}


@app.websocket("/logs/{cpu_hash}")
async def cpu_logs(websocket: WebSocket, cpu_hash: str):
    c = MapAPI.instance.computer_find(cpu_hash.encode())

    if c is None:
        return "No such computer"

    await websocket.accept()

    loop = asyncio.get_running_loop()

    def event(d: Optional[bytes]):
        if d:
            loop.create_task(websocket.send_text(d.decode()))
        else:
            websocket.close()

    c.subscribe_events(event)

    for e in c.last_events:
        await websocket.send_text(e.decode())

    while True:
        try:
            text = await websocket.receive_text()
        except WebSocketDisconnect:
            c.unsubscribe_events(event)
            break
        await websocket.send_text(f"You said: {text}")


@app.get("/cpu/{cpu_hash}", response_class=HTMLResponse)
async def get_cpu(request: Request, cpu_hash: str, q: str = None):

    c = MapAPI.instance.computer_find(cpu_hash.encode())

    if c is None:
        return "No such computer"

    hostname = c.get_hostname().decode()
    powered_on = c.is_powered_on()
    cpu_status = "Powered ON" if powered_on else "Turned OFF"

    return templates.TemplateResponse(
        name="cpu.html", context={
            "id": c.get_hash().decode(),
            "cpu_name": hostname,
            "powered_on": powered_on,
            "cpu_status": cpu_status,
            "cpu_hash": cpu_hash,
            "request": request
        }
    )


@app.post("/cpu/{cpu_hash}/snapshot")
async def post_cpu(request: Request, snapshot: UploadFile, cpu_hash: str):
    c = MapAPI.instance.computer_find(cpu_hash.encode())

    if c is None:
        return "No such computer"

    if not c.load_snapshot(await snapshot.read()):
        c.notify_event(b"Failed to load snapshot")
        return "Failed to load snapshot"

    return "OK"


def run_server():
    uvicorn.run(app, host="0.0.0.0", port=8080)


def start_http_server():
    server_thread = threading.Thread(target=run_server)
    server_thread.daemon = True
    server_thread.start()
