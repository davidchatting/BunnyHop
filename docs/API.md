# Bridge API

One JSON command format is shared by three transports:

| Transport | How |
|---|---|
| WebSocket | `ws://192.168.4.1/ws` - send a command object, get an `ack`; events arrive unsolicited |
| REST | `POST /api/cmd` with a command object as the body; reply is the ack (HTTP 409 if `ok` is false) |
| Serial | one command object per line at 115200 baud (plus shortcuts, see README) |

The JavaScript wrapper `firmware/data/www/js/rabbit-api.js` (`RabbitClient`)
covers all of this, with promises, reconnection and throttling.

## Commands

Every command may carry an `"id"`; the ack echoes it.

```json
{"id": 7, "cmd": "ears", "left": 4, "right": 12}
→ {"type": "ack", "id": 7, "cmd": "ears", "ok": true}
→ {"type": "ack", "id": 7, "cmd": "ears", "ok": false, "error": "rabbit not connected"}
```

`left`/`right` are always **the rabbit's own** sides. Colours are `"#rrggbb"`,
`"#rgb"` or `[r, g, b]`. LED ids: 0 bottom, 1 left, 2 middle, 3 right, 4 nose.

| cmd | Fields | Effect |
|---|---|---|
| `state` | - | `data`: rabbit state (below) |
| `wifi` | - | `data`: AP, pairing and rabbit identity |
| `ears` | `left`, `right` (0-16, -1 = leave) | move ears |
| `led` | `led` (0-4, -1 = all), `color` | set one LED, keep the others |
| `leds` | `colors`: 5 colours | set all LEDs |
| `play` | `url`, `wait` (default true) | play audio; `/audio/x.mp3` = a file on the bridge |
| `stream` | `url` | stream audio (`ST`) |
| `chor` | `tempo` (10 ms units, default 10), `steps` | run a choreography (below) |
| `program` | `text` | raw Violet program, sent as-is (`"MU ...\nMW"`) |
| `ambient` | `services`: `[[id, value], ...]` | raw ambient services (8 = nose blink 0-2, 1 = weather 0-5, ...) |
| `sleep`, `wake`, `reboot` | - | rabbit mode / reboot |
| `raw` | `hex` | raw packet `7f ... ff` |
| `xmpp` | `xml` | send any stanza to the rabbit (debugging) |
| `diag` | `node` (`getrunningstate`/`getconfig`) | ask the bootcode for its state (it currently doesn't reply) |
| `audio` | - | `data`: list of audio files on the bridge |
| `log` | - | `data`: recent log lines |
| `time` | `epochMs` | set the bridge clock (served to the rabbit over NTP) |
| `lan` | `ssid`, `password` (empty `ssid` = forget) | save home Wi-Fi and connect |
| `lanScan` | - | scan for home networks; result arrives as a `lanScan` event |
| `pair` | - | scan for a rabbit in setup mode and provision it |
| `forget` | - | forget provisioning (rabbit, AP SSID) |
| `tune` | `linger` (ms) | resource-server linger before close (default 300) |

### Choreography steps

Steps run in order; `delay` is the number of frames to wait before the step.

```json
{"cmd": "chor", "tempo": 10, "steps": [
  {"type": "led",   "led": 1, "color": "#ff0000"},
  {"type": "motor", "ear": 0, "pos": 8, "dir": 0, "delay": 50},
  {"type": "all",   "color": "#0000ff", "delay": 50}
]}
```

`motor`: `ear` 0 = rabbit's left, 1 = right; `pos` 0-16; `dir` 0 forward, 1 backward.
`all` sets every LED (bootcode opcode 9).

Choreographies (including `led`/`leds`) are coalesced: while one is being
downloaded by the rabbit, newer requests replace each other and only the
latest is sent, at most every 1.5 s. The ack for a coalesced request is
`ok: true` immediately.

Note: `led`/`leds` send a choreography that holds the colours for up to an
hour, because the rabbit only shows LED colours while a choreography runs.
Any later program (another LED command, audio, chor) or a button click ends it.

## Events (WebSocket)

| type | Fields |
|---|---|
| `hello` | `state`, `wifi`, `clients` - sent on connect |
| `state` | `state`, `clients` - every 2 s |
| `connection` | `connected` |
| `phase` | `phase`: `idle`, `bootcode`, `locate`, `xmpp-auth`, `boot`, `ready` |
| `button` | `clicks` (1 or 2), `event` (id of interrupted program, if any) |
| `ears` | `left`, `right` - ears moved by hand |
| `rfid` | `tag` |
| `recording` | `url` (playable PCM WAV), `bytes` |
| `boot` | `loader`, `mac` - rabbit requested the bootcode |
| `resource` | `resource` - XMPP resource bound |
| `pairing` | `step`, `ssid` / `ap` |
| `scan` | `found` |
| `diagnostics` | `node`, `fields` |
| `lan` | `state` (`connected`/`unavailable`), `ip` |
| `lanScan` | `networks`: `[{ssid, rssi, open}]` |
| `log` | `cat`, `msg` |

All events carry `t` (bridge uptime, ms).

### State object

```json
{"phase": "ready", "connected": true, "resource": "idle", "sn": "0013d382de00",
 "ip": "192.168.4.2", "bootcodeRev": "21029", "lastSeenMsAgo": 1200, "asleep": false,
 "ears": {"left": -1, "right": -1, "cmdLeft": 0, "cmdRight": 10},
 "leds": ["#000000", "#ff0000", "#00ff00", "#0000ff", "#ffffff"],
 "lastTag": "", "lastClick": 1, "lastRecording": "/rec/rec0-pcm.wav"}
```

`ears.left/right` are positions reported by the rabbit (-1 = none yet);
`cmdLeft/cmdRight` and `leds` are what was last commanded (the rabbit can't
report LED colours).

### Wi-Fi status (`wifi` command)

```json
{"ap": "Nabaztag82DE00", "private": true, "apClients": 1, "rabbitSn": "0013d382de00",
 "lan": {"ssid": "UniFi", "state": "connected", "ip": "192.168.1.71", "host": "bunnyhop.local"}, ...}
```

`lan.state`: `unset`, `connecting`, `connected`, `unavailable`. `private`: the
rabbit's network is hidden and rabbit-only (home Wi-Fi is up). The password is
never returned.

## REST

| Method | Path | |
|---|---|---|
| GET | `/api/state` | rabbit + wifi state, client count, uptime, heap |
| POST | `/api/cmd` | JSON command |
| GET | `/api/audio` | list audio files |
| POST | `/api/audio` | multipart upload (field `file`) |
| DELETE | `/api/audio?name=x.mp3` | delete |
| GET | `/api/fs` | flash filesystem usage |
| GET | `/audio/<file>`, `/rec/<file>` | audio files and recordings for browsers |

Example: `curl -d '{"cmd":"leds","colors":["#000","#f00","#0f0","#00f","#fff"]}' http://192.168.4.1/api/cmd`
