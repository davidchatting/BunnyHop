# Setting up BunnyHop

## Hardware

- Nabaztag:tag (tested: serial `00:13:d3:82:de:00`, loader firmware `0.0.0.10`).
  **Turn the volume wheel up** - at zero, audio "plays" silently.
- An ESP32 with 4 MB flash (tested: Adafruit Feather ESP32 / ESP32-D0WDQ6, CP2104 USB-serial).
- Optional for development: a USB camera with microphone pointed at the rabbit.

## Install

### From the browser

The **Build firmware and web flasher** GitHub Action builds the firmware and
the LittleFS image on every push to `main` and makes a web flasher
([ESP Web Tools](https://esphome.github.io/esp-web-tools/)): Chrome or Edge,
**Connect**, pick the ESP32's serial port, install. The parts are flashed at
their own offsets, so the NVS partition (rabbit pairing, home Wi-Fi) survives
unless you tick **Erase device**.

The flasher is deployed to GitHub Pages once the repository is public (Pages
isn't available for this private repo). Until then, download the
`bunnyhop-firmware` artifact from the latest run under **Actions**, and serve
it locally (`python3 -m http.server` in the unzipped folder, open
`http://localhost:8000/` - Web Serial allows `localhost` without HTTPS).

### With PlatformIO

Requires [PlatformIO](https://platformio.org/).

```sh
cd firmware
pio run -t upload        # firmware
pio run -t uploadfs      # web app, bootcode, sample audio (LittleFS)
```

`firmware/platformio.ini` pins `espressif32@6.10.0` (Arduino core 2.0.17).

## First-time setup

1. Power the ESP32. It starts an AP called `BunnyHop` and scans for a
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
6. Optionally put BunnyHop on your home Wi-Fi (see [Networks](#networks) below).

The network is **open** (no password) - the rabbit's 2005 Wi-Fi stack needs
it. On later boots the bridge restores its AP and the rabbit reconnects by
itself. If the rabbit doesn't appear for 3 minutes, the bridge rescans for a
rabbit in setup mode every minute; "Find & pair rabbit" in the web app or
`pair` on the serial console does it on demand.

## Networks

The ESP32 has one radio. It hosts **one** network - the rabbit's - and can at
the same time join your **home Wi-Fi**:

| | Home Wi-Fi connected | Home Wi-Fi not set or unreachable |
|---|---|---|
| Rabbit's network (`Nabaztag82DE00`) | **Private**: no password, **hidden**, and every device except the rabbit is disconnected straight away | **Public**: visible and open; joining it pops up the web app (captive portal) |
| Web app | `http://bunnyhop.local/` on your home network | `http://192.168.4.1/` on the rabbit's network |

- Set home Wi-Fi in the web app: **Connection → Home Wi-Fi → Scan**, pick the
  network, enter the password, **Save & connect**. The device you used is
  disconnected from the rabbit's network once BunnyHop is online; rejoin your
  home Wi-Fi and open `http://bunnyhop.local/`. **Forget** reverses this.
- You never have to set home Wi-Fi: without it BunnyHop works entirely on the
  rabbit's network.
- If home Wi-Fi drops, the rabbit's network becomes public again at once and
  BunnyHop retries every minute (each attempt is capped at 15 s, because a
  searching ESP32 disturbs the rabbit's network). When it reconnects, other
  devices are disconnected again.
- The rabbit's network always uses the home Wi-Fi's channel. The rabbit follows
  channel changes and finds the hidden network by itself (tested: ready again
  within 20-40 s); if it ever gets stuck, the stale-session watchdog restarts it.
- Online, BunnyHop also sets its clock from the internet and passes real time
  to the rabbit.
- Home Wi-Fi credentials are stored on the ESP32 in plain text (NVS).
