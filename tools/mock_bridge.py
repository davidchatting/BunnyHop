#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Mock bridge for working on the web app without hardware.

Serves firmware/data/www over HTTP and speaks the bridge's WebSocket API
(/ws) with a simulated rabbit: commands are acknowledged and reflected in
state, and random button/RFID/ear events are generated.

    python3 tools/mock_bridge.py [--port 8080]   ->  http://localhost:8080/

Needs the `websockets` package (>= 13).
"""
import argparse, asyncio, json, mimetypes, os, random, time
from http import HTTPStatus
from websockets.asyncio.server import serve
from websockets.http11 import Response
from websockets.datastructures import Headers

ROOT = os.path.join(os.path.dirname(__file__), "..", "firmware", "data", "www")
AUDIO = os.path.join(os.path.dirname(__file__), "..", "firmware", "data", "audio")


def audio_list():
    """The sample sounds, listed as the bridge's /api/audio does."""
    return [{"name": n, "url": "/audio/" + n, "bytes": os.path.getsize(os.path.join(AUDIO, n))}
            for n in sorted(os.listdir(AUDIO))] if os.path.isdir(AUDIO) else []
T0 = time.time()
clients = set()
state = {
    "phase": "ready", "connected": True, "resource": "idle", "sn": "0013d3000001",
    "ip": "192.168.4.2", "bootcodeRev": "mock", "lastSeenMsAgo": 0, "asleep": False,
    "ears": {"left": 0, "right": 0, "cmdLeft": -1, "cmdRight": -1},
    "leds": ["#000000"] * 5, "lastTag": "", "lastClick": 0, "lastRecording": "",
}
wifi = {"ap": "BunnyHopM0CK01", "paired": True, "hidden": True, "apClients": 1, "setupSsid": "Nabaztag01",
        "setupBssid": "", "setupInfo": "mock", "rabbitSn": state["sn"], "lastPair": "mock"}
log = []


def ms():
    return int((time.time() - T0) * 1000)


async def broadcast(msg):
    data = json.dumps(msg)
    for ws in list(clients):
        try:
            await ws.send(data)
        except Exception:
            pass


async def add_log(cat, msg):
    log.append({"t": ms(), "cat": cat, "msg": msg})
    del log[:-80]
    await broadcast({"type": "log", "t": ms(), "cat": cat, "msg": msg})


async def execute(c):
    cmd = c.get("cmd", "")
    r = {"type": "ack", "id": c.get("id"), "cmd": cmd, "ok": True}
    if cmd == "state":
        r["data"] = state
    elif cmd == "wifi":
        r["data"] = wifi
    elif cmd == "log":
        r["data"] = log
    elif cmd == "audio":
        r["data"] = audio_list()
    elif cmd == "ears":
        for k, sk in (("left", "cmdLeft"), ("right", "cmdRight")):
            if c.get(k, -1) >= 0:
                state["ears"][sk] = c[k]
                state["ears"][k] = c[k]      # the simulated rabbit gets there at once
    elif cmd == "led":
        ids = range(5) if c.get("led", -1) < 0 else [c["led"]]
        for i in ids:
            state["leds"][i] = c["color"]
    elif cmd == "leds":
        state["leds"] = list(c["colors"])
    elif cmd in ("sleep", "wake"):
        state["asleep"] = cmd == "sleep"
    elif cmd in ("play", "stream", "chor", "program", "ambient", "reboot", "raw", "time", "pair", "forget"):
        pass
    else:
        r.update(ok=False, error=f"unknown cmd '{cmd}'")
    await add_log("cmd", f"{cmd} {json.dumps({k: v for k, v in c.items() if k not in ('cmd', 'id')})}")
    if cmd in ("ears", "led", "leds", "sleep", "wake"):
        await broadcast({"type": "state", "t": ms(), "state": state})  # as the real bridge does
    return r


async def handler(ws):
    clients.add(ws)
    try:
        await ws.send(json.dumps({"type": "hello", "t": ms(), "state": state, "wifi": wifi, "clients": len(clients)}))
        async for m in ws:
            try:
                c = json.loads(m)
            except ValueError:
                continue
            await ws.send(json.dumps(await execute(c)))
    finally:
        clients.discard(ws)


def http(connection, request):
    path = request.path.split("?")[0]
    if path == "/ws":
        return None  # websocket upgrade
    if path == "/api/audio":
        body, ctype = json.dumps(audio_list()).encode(), "application/json"
    else:
        f = os.path.normpath(os.path.join(ROOT, path.lstrip("/") or "index.html"))
        if not f.startswith(os.path.normpath(ROOT)):
            return connection.respond(HTTPStatus.FORBIDDEN, "no\n")
        headers = Headers()
        if os.path.isdir(f):
            f = os.path.join(f, "index.html")
        if not os.path.exists(f) and os.path.exists(f + ".gz"):
            body = open(f + ".gz", "rb").read()
            headers["Content-Encoding"] = "gzip"
        elif os.path.exists(f):
            body = open(f, "rb").read()
        else:
            return connection.respond(HTTPStatus.NOT_FOUND, "not found\n")
        headers["Content-Type"] = mimetypes.guess_type(f)[0] or "application/octet-stream"
        headers["Content-Length"] = str(len(body))
        return Response(200, "OK", headers, body)
    return Response(200, "OK", Headers({"Content-Type": ctype, "Content-Length": str(len(body))}), body)


async def simulate():
    tags = ["d0021a0352a1b2c3", "d0021a0352a4d5e6"]
    while True:
        await asyncio.sleep(random.uniform(4, 9))
        kind = random.choice(["button", "rfid", "ears", "state"])
        if kind == "button":
            await broadcast({"type": "button", "t": ms(), "clicks": random.choice([1, 1, 2])})
        elif kind == "rfid":
            tag = random.choice(tags)
            state["lastTag"] = tag
            await broadcast({"type": "rfid", "t": ms(), "tag": tag})
        elif kind == "ears":
            l, r = random.randint(0, 16), random.randint(0, 16)
            state["ears"].update(left=l, right=r)
            await broadcast({"type": "ears", "t": ms(), "left": l, "right": r})
        await broadcast({"type": "state", "t": ms(), "state": state, "clients": len(clients)})


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--offline", action="store_true", help="act as if the rabbit isn't connected")
    a = ap.parse_args()
    if a.offline:
        state.update(connected=False, phase="idle")
    async with serve(handler, "0.0.0.0", a.port, process_request=http):
        print(f"mock bridge on http://localhost:{a.port}/")
        await simulate()


asyncio.run(main())
