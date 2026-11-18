# Using BunnyHop

## Web app

`http://192.168.4.1/` works on phones, tablets and computers, several at once:

- **The rabbit** - a live drawing that is also the control: tap a light to
  pick its colour, drag an ear to move it. It follows the real rabbit (ears
  moved by hand, changes made from other devices). While the rabbit isn't
  connected it is drawn as a dashed ghost with its lights off.
  Ear positions 0-16 are one full turn: 0-8 the ear swings forward and out,
  8-16 it comes back up the back way, shown by a grey shade (deepest halfway).
  To send an ear round the back, drag it all the way out and back in again.
- **Sounds** - tap to play on the rabbit; "Add a sound…" uploads an MP3.
- **Activity** - button presses, RFID tags, ears moved by hand, and recorded
  messages (long-press the rabbit's button) with a play button.
- **Connection** - Wi-Fi name, rabbit identity, and pairing a new rabbit.

For anything more (choreographies, raw programs, debugging) use the API from
your own code ([API.md](API.md), `rabbit-api.js`) or the serial console.

## Without joining the bridge's Wi-Fi

`python3 tools/serial_bridge.py` serves the web app on http://localhost:8097/
and relays it to the bridge over USB serial - handy on a development machine
without Wi-Fi. Everything except audio upload and recordings works; the
commands run on the real firmware and rabbit. (`tools/mock_bridge.py` serves
the app against a simulated rabbit instead.)

## Rabbit API for your own projects

`firmware/data/www/js/rabbit-api.js` is standalone (no other libraries) and works from
any page served by - or able to reach - the bridge, including p5.js sketches:

```js
const rabbit = new RabbitClient();                 // ws://192.168.4.1/ws
rabbit.on('button', e => rabbit.play('/audio/hello.mp3'));
rabbit.on('rfid', e => console.log(e.tag));
await rabbit.ears(0, 16);                          // rabbit's left, right
await rabbit.leds(['#000', '#f00', '#0f0', '#00f', '#fff']);
```

The underlying WebSocket/REST protocol is documented in [API.md](API.md).

## Serial console

Every API command also works over USB serial (115200 baud) as one line of JSON,
plus shortcuts - useful for experimenting without recompiling:

```
status                      ears 4 12              led 2 #ff0000
play /audio/hello.mp3       prog MU http://...\nMW  raw 7f0b000001 00ff
pair | forget | kick [s]    diag [getrunningstate] dump /rec/rec0.wav
lanpause [s]  (simulate a home Wi-Fi outage)
{"cmd":"leds","colors":["#000","#f00","#0f0","#00f","#fff"]}
{"cmd":"xmpp","xml":"<iq .../>"}      {"cmd":"tune","linger":300}
```

`tools/serial_cmd.py "ears 4 12"` sends commands from a shell. Note that opening
the port with most tools resets the ESP32 (the rabbit then needs ~90 s to
reconnect).
