# The rabbit side: protocol notes

What the bridge does to talk to a Nabaztag:tag, why, and how sure we are.
**[T]** = physically tested on this rabbit, **[P]** = protocol-verified from the
bootcode source / OpenJabNab but not physically observed, **[I]** = inferred.

Sources: Violet bootcode sources and OpenJabNab server code (copies in
`research/`), ServerlessNabaztag firmware sources (same bytecode revision).

## 1. Provisioning [T]

In setup mode (hold the head button while powering on; all LEDs blue) the
rabbit is an open access point `NabaztagXX` at `192.168.0.1` with **no DHCP**
(the bridge uses `192.168.0.100/24`). Its page `GET /` is a single form,
`action="b.htm"`, method GET:

| Field | Meaning | Bridge sends |
|---|---|---|
| `w` | SSID chosen from the rabbit's scan list (`-` = none) | `-` |
| `k` | "OR type your network name" | bridge AP SSID |
| `m` | Encryption: 0 none, 1 WEP, 2 WPA | `0` |
| `n` | Key | empty |
| `l` | WEP auth: 0 OpenSystem, 1 SharedKey | form value |
| `f` | DHCP 1/0 | `1` |
| `g h i j` | Static IP, mask, gateway, DNS | form values |
| `c d e` | Proxy on/off, address, port | form values |
| `a` | "Violet Platform" (host[/path], no scheme) | `192.168.4.1/vl` |
| `z` | Submit button | **`Update and Start`** |

- The page also shows read-only "Serial number" (the MAC), "Login",
  "Password" and "Firmware" (loader version, here `0.0.0.10`); the bridge
  stores these.
- **Without `z=Update and Start` the rabbit saves the settings but stays in
  setup mode.** A browser always sends the clicked submit button's name/value;
  a scripted request must add it. [T]
- There is no separate port field (an earlier project assumed `b`). [T]
- The rabbit's HTTP server sometimes truncates the page; retry until `</form>` arrives. [T]

## 2. Network requirements [T]

- **Open** network (WPA is offered by the form but untested).
- **802.11b-only AP.** With 11g/n enabled the ESP32 transmits OFDM frames the
  rabbit cannot decode.
- Plain alphanumeric SSID (`Nabaztag82DE00`); the bridge avoids punctuation.
- DHCP from the bridge; the rabbit also uses DNS and NTP - the bridge answers
  every DNS name with itself and runs a minimal NTP responder (time is set by
  the first browser that connects).

## 3. Boot sequence [T]

```
rabbit                          bridge
GET /vl/bc.jsp?v=0.0.0.10&m=<mac>&l=..&p=..&h=4   -> 102 354-byte bootcode
GET /vl/locate.jsp?sn=<mac>&h=4&v=21029            -> "ping 192.168.4.1\n
                                                       broad 192.168.4.1\n
                                                       xmpp_domain 192.168.4.1:5222\n"
TCP :5222  XMPP                                    (section 4)
```

The bootcode served is Violet's final release (`bootcode.violet` from OpenJabNab,
bytecode revision 21029), **unmodified**. This revision only supports XMPP: if
`locate.jsp` doesn't return `xmpp_domain` it reboots. `ip:port` in
`xmpp_domain` makes it connect directly instead of doing a DNS SRV lookup. The
download takes ~7 s at 802.11b rates; boot to "ready" ~20 s.

## 4. XMPP [T unless marked]

Mirrors OpenJabNab's `XmppHandler` and auth plugin:

1. Rabbit opens the stream → bridge replies with features: SASL
   `DIGEST-MD5`/`PLAIN` and `violet-register`.
2. First contact: `violet:iq:register` get/set; the rabbit sends
   username = MAC and a password, stored by the bridge as `user XOR password`
   (OpenJabNab convention). [P - this rabbit had registered elsewhere before
   and went straight to auth]
3. `<auth mechanism='DIGEST-MD5'/>` → challenge; on the response the bridge
   verifies the digest if it knows the password, otherwise accepts (like
   OpenJabNab's `StandAloneAuthBypass`) → `<success/>`.
4. Stream restart → features bind/unbind/session.
5. Bind resource `boot`, session, `violet:iq:sources` get → reply with an
   initial packet (nose off + wake) in `<packet>` (base64), bind `idle`,
   session, presence (echoed), unbind `boot`.

**Events from the rabbit** (as `<message>` to `int@xmpp.objects.violet.net/int`):

| Stanza | Meaning |
|---|---|
| `<button><clic>1</clic></button>` | single click [T] |
| `<button><clic>2</clic></button>` | double click [T] |
| `<button><clic>1</clic><event>N</event></button>` | click that interrupted program ID N [T] |
| `<ears><left>L</left><right>R</right></ears>` | ears moved by hand; `left` is the rabbit's **right** ear [T] |

**Commands to the rabbit**: `<message to='<mac>@domain/<resource>'><packet
xmlns='violet:packet' format='1.0' ttl='604800'>BASE64</packet></message>`.

- The bridge addresses packets to the resource **`urgent`**. The bootcode
  (`_isResourceValid`) runs `urgent` packets immediately, but *queues* packets
  for `idle` while a program or choreography is running. [T: ear command
  executed during a running choreography]
- **The rabbit never answered any IQ the bridge sent** (`jabber:iq:version`,
  ad-hoc `getrunningstate`/`getconfig`, single or double quotes, sender domain
  with and without `:5222`), although the bytecode contains those handlers.
  Packet messages work. Cause unknown. [T]
- The bootcode sends no XMPP pings. When the bridge restarts, the rabbit keeps
  a dead session forever. A deauthentication only makes it re-associate; an
  AP outage of 30 s makes it reboot. The bridge does this automatically after
  30 s of silence from an associated rabbit. [T]

## 5. Packets ("trames")

```
0x7F { type(1) length(3, big-endian) data } 0xFF
```

| Type | Data | Effect |
|---|---|---|
| 0x04 ambient | `7F FF FF FE` + (service, value) pairs | services below [T for ears] |
| 0x09 | - | reboot [P] |
| 0x0A program | `0x00` + obfuscated text | runs a program (section 6) [T] |
| 0x0B mode | 1 byte: 0 wake, 1 sleep | [P] |

Ambient services (OpenJabNab numbering): 1 weather, 2 stock market, 3 periph,
**4 / 5 ears (0-16)**, 6 email, 7 air quality, 8 nose blink (0/1/2), 9 bottom
LED, 14 tai chi, 0 = disable a service.

**Ear sides [T]:** with an unmirrored camera, service 4 (Violet's "left ear")
moves the rabbit's **right** ear and service 5 its left. Choreography motor 0
is likewise the rabbit's right ear. The bridge API is rabbit-centric and swaps
these internally; reported `<ears>` are swapped the same way [T: rabbit's left
ear moved by hand arrived as `<right>11</right>`].

Program obfuscation (OpenJabNab "Sache"): `out[0]=0`, then
`out[i] = INV[prev % 128] * c + 47 (mod 256)`, `prev` starting at 35, where INV
is the table of inverses of `2k+1` mod 256.

## 6. Programs

Text lines `KEY value\n`, e.g. `ID 7\nMU http://192.168.4.1:8080/audio/x.mp3\nMW\n`.
Resources named by `MU`/`CH` are **downloaded before the program starts**.

| Key | Effect |
|---|---|
| `MU url` | play audio (MP3 or WAV) [T] |
| `MW` | wait for audio to finish [P] |
| `CH url` | run a choreography file [T] |
| `ST url` / `MS url` | stream audio [P] |
| `PL n` / `CL x` | palette selection / colour [P] |
| `IS` / `IE` | start / end interactive mode [P] |
| `ID n` | program id; echoed in button events [T] |

Audio volume comes only from the wheel on the rabbit (`button3` in the
bootcode). At zero, audio programs run silently. [T]

## 7. Choreographies [T]

```
length(4)  00 01 tempo  { delay(1) opcode args... }  00 00 00 00
```

- Frame duration = `tempo × 10 ms`; each step waits `delay` frames.
- `0x07 led r g b 0 0` - LED colour: 0 bottom, 1 left, 2 middle, 3 right,
  4 nose (left/right = the rabbit's own) [T]
- `0x08 motor pos dir` - ear motor (0 = rabbit's right ear), position 0-16 [T]
- `0x09 r g b` - all LEDs (bootcode v16+) [P]
- Also `0x0A` led off, `0x0E` palette LED, `0x10` random MIDI, `0x11`-`0x14` tai chi helpers [P]

A choreography start blanks all LEDs. When it ends, the idle display takes over
the LEDs again, so the bridge holds LED colours with a long choreography.

## 8. Resource delivery - the big gotcha [T]

Served through ESPAsyncWebServer (which closes the TCP connection as soon as
the data is acknowledged), **~60% of choreographies were silently discarded**:
the rabbit downloaded the file (network-activity LED flashed, server saw a
clean close) but never ran it. Padding the file to several kB only reduced the
failure rate. Sending twice didn't help.

Serving the same files from a small dedicated server (`chor_server`, port
8080) that **keeps the connection open 300 ms after the last ACK** before
closing: **24/24 succeeded** (colour-tracked with the camera), with 1 s and
2 s linger also 12/12. Mechanism inferred: the rabbit's TCP/HTTP code mishandles
a FIN that follows the data too closely. Audio is served the same way. The
linger is tunable live: `{"cmd":"tune","linger":ms}`.

## 9. RFID [P]

On a tag read the bootcode requests
`GET <ping server>/vl/rfid.jsp?sn=<mac>&v=<rev>&h=4&t=<tag hex>` and evaluates
the response as a packet. The bridge answers with an empty body and emits an
`rfid` event. Not yet tested: the reader (ST CRX14, to our knowledge) only
reads ISO 14443-B ST tags (SRI512/SRIX4K - Violet's Ztamps/Nano:ztags); a
non-compatible tag produced no request at all.

## 10. Microphone [T]

A long press records; on release the bootcode POSTs the audio to
`<ping server>/vl/record.jsp?sn=..&v=..&h=4&m=0` (`m=1` for a double-long
press [P]). Format: **IMA ADPCM WAV, mono, 8 kHz, 256-byte blocks** (505
samples/block). Browsers can't play that, so the bridge converts it to 16-bit
PCM WAV (`/rec/recN-pcm.wav`, verified sample-identical to FFmpeg's decode)
and keeps the original.
