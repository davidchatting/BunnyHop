# Setting up BunnyHop

## Hardware

- Nabaztag:tag (tested: serial `00:13:d3:82:de:00`, loader firmware `0.0.0.10`).
  **Turn the volume wheel up** - at zero, audio "plays" silently.
- An ESP32 with 4 MB flash (tested: Adafruit Feather ESP32 / ESP32-D0WDQ6, CP2104 USB-serial).
- Optional for development: a USB camera with microphone pointed at the rabbit.

## Install

### From the browser

Open **https://davidchatting.com/BunnyHop/** in Chrome or Edge on a computer,
plug in the ESP32, press **Connect**, pick its serial port and install. The
parts are flashed at their own offsets, so the rabbit pairing and home Wi-Fi
settings survive unless you tick **Erase device**.

The flasher ([ESP Web Tools](https://esphome.github.io/esp-web-tools/)) is
rebuilt and published to GitHub Pages by the **Build firmware and web
flasher** GitHub Action on every push to `main`. Each run also keeps the files
as a `bunnyhop-firmware` artifact: to use one offline, unzip it, run
`python3 -m http.server` in the folder and open `http://localhost:8000/`
(Web Serial allows `localhost` without HTTPS).

### With PlatformIO

Requires [PlatformIO](https://platformio.org/).

```sh
cd firmware
pio run -t upload        # firmware
pio run -t uploadfs      # web app, bootcode, sample audio (LittleFS)
```

`firmware/platformio.ini` pins `espressif32@6.10.0` (Arduino core 2.0.17).

## First-time setup

1. Power the ESP32. It starts its AP, `BunnyHop` + the last three bytes of
   the ESP32's own MAC address (e.g. `BunnyHop3A1F7C`), and scans for a
   rabbit in setup mode. The name never changes, so you can join it now.
2. Put the rabbit into setup mode: unplug it, hold the button on its head while
   plugging it back in, release when **all LEDs are blue**.
3. Within ~30 s the bridge joins the rabbit's `NabaztagXX` network, reads its
   setup page, and submits it with the bridge's AP as the Wi-Fi network and the
   bridge as the "Violet Platform". The rabbit restarts by itself.
4. The rabbit joins the bridge's AP, downloads the bootcode and logs in.
5. Join the bridge's Wi-Fi network from any device and open **http://192.168.4.1/**.
   (Phones may show it as a captive portal; you can also open it in a browser.)
6. Optionally put BunnyHop on your home Wi-Fi (see [Networks](#networks) below).

The network is **open** (no password) - the rabbit's 2005 Wi-Fi stack needs
it. On later boots the bridge restores its AP and the rabbit reconnects by
itself. If the rabbit doesn't appear for 3 minutes, the bridge rescans for a
rabbit in setup mode every minute; "Pair a rabbit" in the web app or
`pair` on the serial console does it on demand.

A rabbit paired by an older version of BunnyHop was given a different
network name (`Nabaztag` + its own MAC) and has to be paired again.

## Networks

The ESP32 has one radio. It hosts **one** network - the rabbit's - and can at
the same time join your **home Wi-Fi**:

| | Rabbit paired and home Wi-Fi working | Otherwise |
|---|---|---|
| Rabbit's network (`BunnyHop3A1F7C`) | **Private**: no password, **hidden**, and every device except the rabbit is disconnected straight away | **Public**: visible and open; joining it pops up the web app (captive portal) |

Home Wi-Fi counts as working only while its router answers pings (checked
every 30 s, then 10 s after a miss; two silent rounds in a row and it counts
as down). So whenever
`bunnyhop.local` might not be reachable, the rabbit's network is visible and
you can always get in to reconfigure.
| Web app | `http://bunnyhop.local/` on your home network | `http://192.168.4.1/` on the rabbit's network |

- Set home Wi-Fi in the web app: **Connection → Home Wi-Fi → Scan**, pick the
  network, enter the password, **Save & connect**. The device you used is
  disconnected from the rabbit's network once BunnyHop is online; rejoin your
  home Wi-Fi and open `http://bunnyhop.local/`. **Forget** reverses this.
- You never have to set home Wi-Fi: without it BunnyHop works entirely on the
  rabbit's network.
- If home Wi-Fi drops or the router stops answering, the rabbit's network
  becomes public again (at once on a drop, within about 45 s otherwise) and
  BunnyHop retries every minute (each attempt is capped at 15 s, because a
  searching ESP32 disturbs the rabbit's network). When it reconnects, other
  devices are disconnected again.
- The rabbit's network always uses the home Wi-Fi's channel. The rabbit follows
  channel changes and finds the hidden network by itself (tested: ready again
  within 20-40 s); if it ever gets stuck, the stale-session watchdog restarts it.
- Online, BunnyHop also sets its clock from the internet and passes real time
  to the rabbit.
- Home Wi-Fi credentials are stored on the ESP32 in plain text (NVS).
