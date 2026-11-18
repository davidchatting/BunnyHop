# Troubleshooting

| Symptom | Cause / fix |
|---|---|
| Rabbit stays blue after pairing | It saved the settings but wasn't told to restart. The bridge sends `z=Update+and+Start`; if you configure it by hand, click "Update and Start". |
| Rabbit LEDs orange, never connects | It can't join the network. The AP must be open and 802.11b-capable; check `status` on the serial console. Re-pair if the SSID changed. |
| Rabbit silent but "connected" | Volume wheel at zero. |
| Rabbit connected to the AP but never logs in (after the bridge restarted) | It still believes its old XMPP session is alive. The bridge detects this after 30 s and switches the AP off for 30 s, which makes the rabbit reboot. Manually: `kick`. |
| `pio run -t upload` can't find `/dev/ttyUSB0`, `dmesg` shows `can't set config #1, error -32` | The CP2104 failed USB enumeration. `echo 1 \| sudo tee /sys/bus/usb/devices/<port>/bConfigurationValue` (e.g. `2-4`), or replug. |
| `bunnyhop.local` doesn't open | BunnyHop isn't on home Wi-Fi (check Connection on the rabbit's network), or the device doesn't support mDNS - use the IP shown under Connection. |
| Can't join the rabbit's network | It's hidden and rabbit-only while BunnyHop is on home Wi-Fi - use `bunnyhop.local` instead. |
| Web app shows "bridge offline" | You're not on the bridge's Wi-Fi, or the ESP32 restarted; the page reconnects automatically. |
| Clicking the rabbit's button turns LEDs off | A click ends the running program, which includes an LED hold. The click is still reported as an event. |
