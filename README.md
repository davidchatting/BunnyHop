# nabaztag-bridge

An ESP32 that brings a **Nabaztag:tag** (the 2006 Violet rabbit, "V2") back to
life with no internet and no cloud servers. The ESP32 provisions the rabbit
over its own setup network, runs a Wi-Fi access point that the rabbit and any
number of phones/laptops join, speaks the rabbit's original protocol, and
serves a p5.js creative-coding web app where rabbit inputs drive sketches and
sketches drive the rabbit.

**The rabbit's firmware is never modified or flashed.** The rabbit downloads its
program ("bootcode") over the network on every boot; the bridge serves Violet's
original, unmodified bootcode, exactly as Violet's servers once did.

> The original brief said "Nabaztag V1". The physical rabbit is a Nabaztag:tag
> (V2) - RFID and the microphone, both required by the brief, only exist on V2.

```
 phones / laptops ──HTTP + WebSocket──┐
                                      │   ESP32  (AP "Nabaztag82DE00", 192.168.4.1)
                                      │    ├─ web app + REST + WebSocket   :80
 Nabaztag:tag ──HTTP: bc.jsp, locate.jsp, rfid.jsp, record.jsp ──┤   :80
              ──HTTP: choreographies + audio (lingering server) ─┤   :8080
              ──XMPP: login, button/ear events, command packets ─┘   :5222
                                           + DNS catch-all, NTP responder
```

## What works - and how we know

| Feature | Status | Evidence |
|---|---|---|
| Discovery of a rabbit in setup mode (`NabaztagXX`) | **Physically tested** | Serial log |
| Provisioning via the rabbit's own setup page (`/b.htm`) | **Physically tested** | Rabbit restarts and joins the bridge's AP |
| Reading serial number + loader firmware from setup page | **Physically tested** | `00:13:d3:82:de:00`, `0.0.0.10` |
| AP with SSID derived from the rabbit (`Nabaztag` + last 3 MAC bytes) | **Physically tested** | |
| Rabbit boot: bootcode → locate → XMPP login → ready (~20 s) | **Physically tested** | Serial log |
| Automatic reconnect after a bridge restart | **Physically tested** | Recovered in 85 s unattended |
| Ears: left/right positioning, independent | **Physically tested** | Camera |
| LEDs: 5 LEDs, any RGB colour, held until changed | **Physically tested** | Camera, 24/24 colour-tracked trials |
| Choreographies (timed LED + ear sequences) | **Physically tested** | Camera |
| Audio playback (MP3 from the bridge) | **Physically tested** | Camera microphone |
| Button: single and double click | **Physically tested** | Events received while pressing |
| Microphone: long-press recording | **Physically tested** | 2.6 s IMA ADPCM recording received, decoded |
| Sleep / wake packets | Protocol-verified | From bootcode source; not visually confirmed |
| Ears moved by hand → event | **Physically tested** | Left ear moved by hand → `ears` event with correct side |
| RFID tag reads | Protocol-verified | Bootcode calls `/vl/rfid.jsp?t=…`; **no tag tested yet** |
| Nose blink / weather etc. ambient services | Protocol-verified | OpenJabNab service ids |
| Bootcode state queries (`getrunningstate`) | **Not working** | Rabbit doesn't answer our IQs |
| Volume control | **Not available** | Volume comes only from the wheel on the rabbit |

## Hardware

- Nabaztag:tag (tested: serial `00:13:d3:82:de:00`, loader firmware `0.0.0.10`).
  **Turn the volume wheel up** - at zero, audio "plays" silently.
- An ESP32 with 4 MB flash (tested: Adafruit Feather ESP32 / ESP32-D0WDQ6, CP2104 USB-serial).
- Optional for development: a USB camera with microphone pointed at the rabbit.

## Install

Requires [PlatformIO](https://platformio.org/).

```sh
cd firmware
pio run -t upload        # firmware
pio run -t uploadfs      # web app, bootcode, sample audio (LittleFS)
```

`firmware/platformio.ini` pins `espressif32@6.10.0` (Arduino core 2.0.17).

## First-time setup

1. Power the ESP32. It starts an AP called `NabaztagBridge` and scans for a
   rabbit in setup mode.
2. Put the rabbit into setup mode: unplug it, hold the button on its head while
   plugging it back in, release when **all LEDs are blue**.
3. Within ~30 s the bridge joins the rabbit's `NabaztagXX` network, reads its
   setup page, and submits it with the bridge's AP as the Wi-Fi network and the
   bridge as the "Violet Platform". The rabbit restarts by itself.
4. The bridge switches its AP to `Nabaztag` + the rabbit's last three MAC bytes
   (e.g. `Nabaztag82DE00`). The rabbit joins, downloads the bootcode and logs in.
5. Join that Wi-Fi network from any device and open **http://192.168.4.1/**.
   (Phones may show it as a captive portal; you can also open it in a browser.)

The network is **open** (no password) - the rabbit's 2005 Wi-Fi stack needs
it. On later boots the bridge restores its AP and the rabbit reconnects by
itself. If the rabbit doesn't appear for 3 minutes, the bridge rescans for a
rabbit in setup mode every minute; "Find & pair rabbit" in the web app or
`pair` on the serial console does it on demand.

## Using it

### Web app

`http://192.168.4.1/` (works on phones, tablets and computers, several at once):

- **Rabbit** - live drawing: connection state, ear positions, LED colours. Click
  an LED to paint it, drag an ear to move it.
- **Sketch** - a live-coding p5.js editor wired to the rabbit (see below).
- **Ears, LEDs, Audio** - direct controls; upload MP3s to the bridge and play
  them on the rabbit or in the browser.
- **RFID** - live tag reads with counts.
- **Commands** - wake/sleep/reboot, any JSON command, raw Violet programs, raw packets.
- **Event log** - everything the bridge does; tick "protocol detail" for XMPP traffic.
- **Setup & diagnostics** - AP, rabbit identity, connection phase, pairing.

### Sketches

Write ordinary global-mode p5.js. The rabbit is `rabbit`; define handlers to react:

```js
function setup() { createCanvas(400, 200); }

function onButton(e) {            // e.clicks: 1 or 2
  rabbit.leds(['#000', '#f00', '#0f0', '#00f', '#fff']);
}
function onRfid(e)  { print(e.tag); }
function onEars(e)  { print(e.left, e.right); }   // ears moved by hand
function onRecording(e) { new Audio(e.url).play(); }

function draw() {
  background(240);
  if (mouseIsPressed) rabbit.ears(map(mouseX, 0, width, 0, 16), 8, { throttle: true });
}
```

Five examples are built in (Examples… menu). `Ctrl/⌘-Enter` runs.

### Rabbit API for other projects

`firmware/data/www/js/rabbit-api.js` is standalone (no p5.js). Copy it into any
page served from - or able to reach - the bridge:

```js
const rabbit = new RabbitClient();                 // ws://192.168.4.1/ws
rabbit.on('button', e => rabbit.play('/audio/hello.mp3'));
await rabbit.ears(0, 16);
```

The underlying WebSocket/REST protocol is documented in [docs/API.md](docs/API.md).

### Serial console

Every API command also works over USB serial (115200 baud) as one line of JSON,
plus shortcuts - useful for experimenting without recompiling:

```
status                      ears 4 12              led 2 #ff0000
play /audio/hello.mp3       prog MU http://...\nMW  raw 7f0b000001 00ff
pair | forget | kick [s]    diag [getrunningstate] dump /rec/rec0.wav
{"cmd":"leds","colors":["#000","#f00","#0f0","#00f","#fff"]}
{"cmd":"xmpp","xml":"<iq .../>"}      {"cmd":"tune","linger":300}
```

`tools/serial_cmd.py "ears 4 12"` sends commands from a shell. Note that opening
the port with most tools resets the ESP32 (the rabbit then needs ~90 s to
reconnect).

## Architecture

| File | Role |
|---|---|
| `firmware/src/wifi_manager.*` | Scan, provisioning via `/b.htm`, AP, stale-session watchdog |
| `firmware/src/rabbit_http.*` | Rabbit HTTP endpoints: `bc.jsp`, `locate.jsp`, `rfid.jsp`, `record.jsp` |
| `firmware/src/chor_server.*` | Lingering HTTP server for choreographies and audio (:8080) |
| `firmware/src/xmpp_server.*` | Minimal XMPP server for the Violet bootcode (:5222) |
| `firmware/src/packets.*` | Violet packet/"trame", message obfuscation, choreography encoding |
| `firmware/src/rabbit.*` | Rabbit state, events, commands (transport-independent) |
| `firmware/src/api.*` | WebSocket `/ws`, REST `/api/*`, shared JSON command executor |
| `firmware/src/net_services.*` | DNS catch-all, NTP responder (time comes from browsers) |
| `firmware/src/adpcm.*` | Microphone recordings: IMA ADPCM → PCM WAV |
| `firmware/data/vl/bootcode.bin` | Violet's original bootcode (unmodified) |
| `firmware/data/www/` | Web app: `index.html`, `css/style.css`, `js/sketch.js` (main), `js/rabbit-api.js`, `js/controls.js`, `js/events.js`, `js/playground.js`, `lib/p5.min.js.gz` |
| `tools/` | Serial console client, mock bridge for UI work without hardware, camera snapshot |
| `research/` | Reference sources used to establish the protocol |

The ESP32 runs `WIFI_AP_STA`. The radio has one channel, so while the STA side
is joined to the rabbit's setup network the AP follows that channel; after
provisioning the AP returns to channel 1. The AP is restricted to 802.11b:
with g/n enabled the ESP32 sends OFDM frames the rabbit can't receive.

Rabbit-side protocol details, including the problems found and fixed, are in
[docs/PROTOCOL.md](docs/PROTOCOL.md).

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| Rabbit stays blue after pairing | It saved the settings but wasn't told to restart. The bridge sends `z=Update+and+Start`; if you configure it by hand, click "Update and Start". |
| Rabbit LEDs orange, never connects | It can't join the network. The AP must be open and 802.11b-capable; check `status` on the serial console. Re-pair if the SSID changed. |
| Rabbit silent but "connected" | Volume wheel at zero. |
| Rabbit connected to the AP but never logs in (after the bridge restarted) | It still believes its old XMPP session is alive. The bridge detects this after 30 s and switches the AP off for 30 s, which makes the rabbit reboot. Manually: `kick`. |
| `pio run -t upload` can't find `/dev/ttyUSB0`, `dmesg` shows `can't set config #1, error -32` | The CP2104 failed USB enumeration. `echo 1 \| sudo tee /sys/bus/usb/devices/<port>/bConfigurationValue` (e.g. `2-4`), or replug. |
| Web app shows "bridge offline" | You're not on the bridge's Wi-Fi, or the ESP32 restarted; the page reconnects automatically. |
| Clicking the rabbit's button turns LEDs off | A click ends the running program, which includes an LED hold. The click is still reported as an event. |

## Known limitations

- **LED colours only persist inside a choreography.** The bootcode's idle display
  blanks the LEDs when a choreography ends, so LED commands send an hour-long
  "hold" choreography. A button click, a sleep, or another program ends it.
  Outside choreographies the bootcode drives the LEDs itself (per its source,
  a breathing base LED and info-service animations).
- **Latency:** ears start moving within ~1 s; LEDs/choreographies ~0.9 s; audio ~2-3 s (the rabbit
  downloads the whole resource before playing).
- **No volume control** (hardware wheel only); **MP3/WAV only from the bridge**
  (the rabbit can't reach the internet).
- **Open Wi-Fi network**: anyone in range can join and control the rabbit.
- **RFID not yet tested with a physical tag.**
- **The rabbit ignores XMPP IQs** from the bridge, so its internal state
  (`getrunningstate`) can't be read; diagnosis relies on its observable behaviour.
- Single rabbit per bridge.
- Large recordings are kept on flash (4 rotating slots, ~2 MB filesystem).

## Future development

- Test RFID with real tags.
- Optional WPA (the setup page offers WPA; unknown which ciphers the rabbit supports).
- Text-to-speech on the bridge (e.g. a tiny formant synthesiser) - the rabbit plays any MP3/WAV the bridge serves.
- Streaming audio (`ST` programs) from a browser's microphone through the bridge.
- Interactive mode (`IS`/`IE` programs) for faster button/ear feedback.
- Getting the rabbit to answer IQs (would expose its internal state for diagnostics).
- Persistent sketch library on the bridge; OTA firmware updates.

## Credits and licences

This project stands on years of community reverse-engineering:

- **Violet / Mindscape** - the Nabaztag and its bootcode (by Sylvain Huet et al.).
  The original compiled bootcode served by the bridge (`firmware/data/vl/bootcode.bin`)
  and the bootcode sources consulted in `research/violet-src/` come from the
  OpenJabNab repository below.
- **OpenJabNab** - https://github.com/OpenJabNab/OpenJabNab (GPL-2.0). The XMPP
  dialogue (auth, bind, sources, packet messages), packet formats, message
  obfuscation table, choreography format and LED/ear numbering follow its
  server (`server/lib`, `server/plugins/system`). `bootcode.violet` is from
  `http-wrapper/ojn_local/bootcode/`.
- **ServerlessNabaztag** by andreax79 - https://github.com/andreax79/ServerlessNabaztag
  (no licence stated). Used as a reference for the bootcode's internals (same
  bytecode revision, 21029); no code copied, and its replacement firmware is
  *not* used.
- **BunnyHop** and **nabaztag-controller** - earlier local experiments with this
  same rabbit; their notes on 802.11b-only radios and the boot sequence saved
  a lot of time (one of BunnyHop's conclusions - a `b=` port field - turned out
  to be wrong).
- **p5.js** 1.11.3 - https://p5js.org (LGPL-2.1), served from the bridge.
- **ESPAsyncWebServer** and **AsyncTCP** (ESP32Async) - https://github.com/ESP32Async (LGPL-3.0).
- **ArduinoJson** by Benoît Blanchon - https://arduinojson.org (MIT).
- **Arduino-ESP32** / ESP-IDF (Espressif, Apache-2.0) and LittleFS.
- Sample audio was generated for this project (tones with FFmpeg, speech with eSpeak NG).

This project's own licence has not been chosen yet. Because it redistributes
OpenJabNab's copy of the Violet bootcode and follows OpenJabNab's GPL-2.0 code
closely, GPL-2.0-or-later is the natural fit.

## Prompt log

The prompts used to build this project are in [PROMPTS.md](PROMPTS.md).
