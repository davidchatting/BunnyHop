#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the web app on this computer against a real bridge over USB serial.

For machines that can't join the bridge's Wi-Fi: serves firmware/data/www
locally and relays the WebSocket API to the ESP32's serial console, where the
same command executor (api::execute) runs. Events come back as `EVT {json}`
lines, log lines as `[time] cat msg`.

    python3 tools/serial_bridge.py [--port /dev/ttyUSB0] [--http 8097]
    -> http://localhost:8097/

Not covered: the ESP32's own HTTP/WebSocket server, audio upload, recordings
download (they live on the ESP32's flash). Opening the serial port resets
most ESP32 boards; the rabbit reconnects by itself (~90 s).
Needs `pyserial` and `websockets` (>= 13).
"""
import argparse, asyncio, json, mimetypes, os, re, threading, time
import serial
from websockets.asyncio.server import serve
from websockets.datastructures import Headers
from websockets.http11 import Response

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "firmware", "data", "www")
AUDIO = os.path.join(ROOT, "..", "audio")
LOG_RE = re.compile(r"^\[\s*(\d+)\.(\d+)\]\s+(\S+)\s+(.*)$")

clients = set()
pending = {}  # command id -> future
next_id = [100000]
loop = None
ser = None
T0 = time.time()


def ms():
    return int((time.time() - T0) * 1000)


async def broadcast(msg):
    data = json.dumps(msg)
    for ws in list(clients):
        try:
            await ws.send(data)
        except Exception:
            pass


def on_line(line):
    """Called from the serial reader thread for each line."""
    if line.startswith("EVT "):
        try:
            asyncio.run_coroutine_threadsafe(broadcast(json.loads(line[4:])), loop)
        except ValueError:
            pass
    elif line.startswith("{"):
        try:
            msg = json.loads(line)
        except ValueError:
            return
        fut = pending.pop(msg.get("id"), None)
        if fut:
            loop.call_soon_threadsafe(lambda: fut.done() or fut.set_result(msg))
    else:
        m = LOG_RE.match(line)
        if m:
            asyncio.run_coroutine_threadsafe(
                broadcast({"type": "log", "t": ms(), "cat": m.group(3), "msg": m.group(4)}), loop)


def reader():
    buf = b""
    while True:
        buf += ser.read(4096)
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            on_line(raw.decode("utf-8", "replace").rstrip("\r"))


async def command(cmd, timeout=6):
    """Send one command to the ESP32 and wait for its reply."""
    cid = next_id[0]
    next_id[0] += 1
    fut = loop.create_future()
    pending[cid] = fut
    ser.write((json.dumps(dict(cmd, id=cid)) + "\n").encode())
    try:
        return await asyncio.wait_for(fut, timeout)
    except asyncio.TimeoutError:
        pending.pop(cid, None)
        return {"ok": False, "error": "no reply from bridge (serial)"}


async def handler(ws):
    clients.add(ws)
    try:
        st, wi = await command({"cmd": "state"}), await command({"cmd": "wifi"})
        await ws.send(json.dumps({"type": "hello", "t": ms(), "state": st.get("data", {}),
                                  "wifi": wi.get("data", {}), "clients": len(clients)}))
        async for m in ws:
            try:
                c = json.loads(m)
            except ValueError:
                continue
            client_id = c.pop("id", None)
            r = await command(c)
            r.update(type="ack", id=client_id)
            await ws.send(json.dumps(r))
    finally:
        clients.discard(ws)


async def state_pusher():
    while True:
        await asyncio.sleep(2)
        if clients:
            r = await command({"cmd": "state"})
            if r.get("ok"):
                await broadcast({"type": "state", "t": ms(), "state": r["data"], "clients": len(clients)})


def http(connection, request):
    path = request.path.split("?")[0]
    if path == "/ws":
        return None
    if path == "/api/audio":
        # the files flashed to the bridge are the ones in firmware/data/audio
        files = [{"name": n, "url": "/audio/" + n, "bytes": os.path.getsize(os.path.join(AUDIO, n))}
                 for n in sorted(os.listdir(AUDIO))]
        body = json.dumps(files).encode()
        return Response(200, "OK", Headers({"Content-Type": "application/json", "Content-Length": str(len(body))}), body)
    base = AUDIO if path.startswith("/audio/") else ROOT
    rel = path[len("/audio/"):] if base is AUDIO else (path.lstrip("/") or "index.html")
    f = os.path.normpath(os.path.join(base, rel))
    if not f.startswith(os.path.normpath(base)):
        return connection.respond(403, "no\n")
    headers = Headers()
    if os.path.exists(f + ".gz") and not os.path.exists(f):
        body = open(f + ".gz", "rb").read()
        headers["Content-Encoding"] = "gzip"
    elif os.path.isfile(f):
        body = open(f, "rb").read()
    else:
        return connection.respond(404, "not on this computer (lives on the bridge)\n")
    headers["Content-Type"] = mimetypes.guess_type(f)[0] or "application/octet-stream"
    headers["Content-Length"] = str(len(body))
    return Response(200, "OK", headers, body)


async def main():
    global loop, ser
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--http", type=int, default=8097)
    a = ap.parse_args()
    loop = asyncio.get_running_loop()
    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = a.port, 115200, 0.05
    ser.dtr = ser.rts = False
    ser.open()
    threading.Thread(target=reader, daemon=True).start()
    async with serve(handler, "0.0.0.0", a.http, process_request=http):
        print(f"web app on http://localhost:{a.http}/ (relaying to {a.port})", flush=True)
        await state_pusher()


asyncio.run(main())
