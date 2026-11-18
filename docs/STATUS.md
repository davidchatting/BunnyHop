# Status

What has been tested, and how. Open problems are tracked as
[GitHub issues](https://github.com/davidchatting/BunnyHop/issues).

> The original brief said "Nabaztag V1". The physical rabbit is a Nabaztag:tag
> (V2) - RFID and the microphone, both required by the brief, only exist on V2.

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
| Audio playback (MP3 from the bridge) | **Physically tested** | Camera microphone; Big Ben chime confirmed by ear |
| Button: single and double click | **Physically tested** | Events received while pressing |
| Microphone: long-press recording | **Physically tested** | 2.6 s IMA ADPCM recording received, decoded |
| Web app on iPad (touch: tap lights, drag ears) | **Physically tested** | Confirmed by the user on a real iPad (previous p5.js drawing; the new SVG rabbit is not yet tested on hardware) |
| Home Wi-Fi + `http://bunnyhop.local/` | **Physically tested** | Joined the user's network; page served over mDNS |
| Rabbit network hidden + rabbit-only while home Wi-Fi is up | **Physically tested** | Rabbit followed the channel change and rejoined the hidden network in ~20-40 s |
| Rabbit network public again when home Wi-Fi drops | **Physically tested** (bridge side) | Simulated outage (`lanpause`): network became visible/open, rabbit rejoined in 37 s; another device joining not yet confirmed |
| Sleep / wake packets | Protocol-verified | From bootcode source; not visually confirmed |
| Ears moved by hand → event | **Physically tested** | Left ear moved by hand → `ears` event with correct side |
| RFID tag reads | Protocol-verified | Bootcode calls `/vl/rfid.jsp?t=…`; **untested - no compatible tag available** (see below) |
| Nose blink / weather etc. ambient services | Protocol-verified | OpenJabNab service ids |
| Bootcode state queries (`getrunningstate`) | **Not working** | Rabbit doesn't answer our IQs |
| Volume control | **Not available** | Volume comes only from the wheel on the rabbit |

## Known limitations

- **LED colours only persist inside a choreography.** The bootcode's idle display
  blanks the LEDs when a choreography ends, so LED commands send an hour-long
  "hold" choreography. A button click, a sleep, or another program ends it.
  Outside choreographies the bootcode drives the LEDs itself (per its source,
  a breathing base LED and info-service animations).
- **LED update rate:** the rabbit blanks its LEDs for ~0.7 s while it downloads
  each new choreography, so the bridge shows each LED update for at least
  1.5 s and keeps only the latest pending one. For smooth animation, send one
  multi-step choreography (`rabbit.chor(...)`) rather than streaming colours.
- **Latency:** ears start moving within ~1 s; LEDs/choreographies ~0.9 s; audio ~2-3 s (the rabbit
  downloads the whole resource before playing).
- **No volume control** (hardware wheel only); **MP3/WAV only from the bridge**
  (the rabbit can't reach the internet).
- **Open Wi-Fi network** while home Wi-Fi isn't set or reachable: anyone in range
  can join and control the rabbit. With home Wi-Fi it is hidden and rabbit-only,
  but anyone on your home network can open `bunnyhop.local`.
- **RFID not yet tested with a physical tag.** The reader (ST CRX14, to our
  knowledge) only sees ISO 14443-B ST tags (SRI512/SRIX4K family - Violet's
  Ztamps and Nano:ztags). Common MIFARE/NTAG (14443-A) cards and stickers are
  ignored; a tag tried during development produced no request at all.
- **The rabbit ignores XMPP IQs** from the bridge, so its internal state
  (`getrunningstate`) can't be read; diagnosis relies on its observable behaviour.
- Single rabbit per bridge.
- Large recordings are kept on flash (4 rotating slots, ~2 MB filesystem).

## Future development

- Test RFID with an ISO 14443-B ST tag (e.g. a Ztamp, or an SRIX4K card).
- Optional WPA (the setup page offers WPA; unknown which ciphers the rabbit supports).
- Text-to-speech on the bridge (e.g. a tiny formant synthesiser) - the rabbit plays any MP3/WAV the bridge serves.
- Streaming audio (`ST` programs) from a browser's microphone through the bridge.
- Interactive mode (`IS`/`IE` programs) for faster button/ear feedback.
- Getting the rabbit to answer IQs (would expose its internal state for diagnostics).
- Persistent sketch library on the bridge; OTA firmware updates.
