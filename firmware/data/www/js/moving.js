// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * moving.js - when this page loses BunnyHop because a Wi-Fi network changed,
 * say on screen which network to join, and move the page to wherever
 * BunnyHop answers next.
 *
 * BunnyHop is reachable at up to three addresses:
 *   http://192.168.4.1/      on its own network (BunnyHopXXXXXX)
 *   http://bunnyhop.local/   on home Wi-Fi (mDNS)
 *   http://<home IP>/        on home Wi-Fi (for devices without mDNS)
 * While the connection is lost it tries this page's own address first (the
 * page just reconnects if that comes back), then the others; the first that
 * answers wins.
 *
 * Controls call Moving.expect(kind, ...) before an action that will cut this
 * page off, so the message can say exactly what is happening.
 */
const Moving = (() => {
  const AP_URL = 'http://192.168.4.1/';
  const MDNS_URL = 'http://bunnyhop.local/';
  const PROBE_MS = 2500;   // per address
  const ROUND_MS = 3000;   // between rounds
  const EXPECT_MS = 3 * 60 * 1000;

  let rabbit;
  let known = { ap: '', lan: '', lanIp: '', private: false, paired: false };  // last seen
  let expected = null;     // { kind, ssid, until }
  let lost = false, timer = null, panel;

  const here = () => location.origin + '/';
  const onAp = () => location.hostname === '192.168.4.1';

  function remember(w) {
    if (!w) return;
    const lan = w.lan || {};
    known.ap = w.ap || known.ap;
    known.lan = lan.ssid != null ? lan.ssid : known.lan;
    known.lanIp = lan.state === 'connected' && lan.ip ? lan.ip : known.lanIp;
    known.private = !!w.private;
    known.paired = !!w.paired;
  }

  function expect(kind, ssid) {
    expected = { kind, ssid: ssid || '', until: Date.now() + EXPECT_MS };
  }

  const b = (s) => '<b>' + String(s).replace(/[&<>"]/g, (c) => '&#' + c.charCodeAt(0) + ';') + '</b>';
  const ap = () => b(known.ap || 'BunnyHop…');

  // What to tell the user: [headline, what to do]
  function message() {
    const e = expected && expected.until > Date.now() ? expected : null;
    const home = b((e && e.ssid) || known.lan || 'your home Wi-Fi');
    if (e && e.kind === 'pair') return ['BunnyHop is pairing with a rabbit',
      'It leaves ' + home + ' for about 30 seconds while it talks to the rabbit. This page reconnects by itself.'];
    if (e && e.kind === 'lan-join' && onAp() && !known.paired) return ['Reconnecting to ' + ap(),
      'BunnyHop moved its network to the channel of ' + home + '. This device should rejoin ' + ap() + ' by itself; on ' + home + ' it is also at bunnyhop.local.'];
    if (e && e.kind === 'lan-join' && onAp()) return ['BunnyHop has joined ' + home,
      ap() + ' is now private to the rabbit. Join ' + home + ' on this device and this page moves to bunnyhop.local.'];
    if (e && e.kind === 'lan-join') return ['BunnyHop is moving to ' + home,
      'Join ' + home + ' on this device too and this page reconnects there.'];
    if (e && e.kind === 'lan-forget') return ['BunnyHop has left ' + b(known.lan || 'home Wi-Fi'),
      'Join ' + ap() + ' on this device (it is visible and open now) and this page moves to 192.168.4.1.'];
    if (onAp() && known.private) return ['BunnyHop is back on ' + home,
      ap() + ' is private to the rabbit again. Join ' + home + ' on this device and this page moves to bunnyhop.local.'];
    if (onAp()) return ['Lost ' + ap(),
      'Check this device is still on ' + ap() + '. If BunnyHop is on ' + home + ', join that instead and this page follows.'];
    return ["Can't reach BunnyHop on " + home,
      'If ' + home + ' is down, join ' + ap() + ' on this device: it becomes visible within a minute, and this page moves there.'];
  }

  function candidates() {
    const list = [here()];
    if (onAp()) list.push(MDNS_URL, known.lanIp && 'http://' + known.lanIp + '/', AP_URL);
    else list.push(AP_URL, MDNS_URL, known.lanIp && 'http://' + known.lanIp + '/');
    return [...new Set(list.filter(Boolean))];
  }

  // Any answer at all (even a 404) means BunnyHop is there; no-cors because
  // the other addresses are other origins.
  function probe(url) {
    const ctl = new AbortController();
    const t = setTimeout(() => ctl.abort(), PROBE_MS);
    return fetch(url + 'js/rabbit-api.js?probe=' + Date.now(), { mode: 'no-cors', cache: 'no-store', signal: ctl.signal })
      .then(() => true, () => false)
      .finally(() => clearTimeout(t));
  }

  function render(trying) {
    const [title, body] = message();
    panel.querySelector('h2').innerHTML = title;
    panel.querySelector('.what').innerHTML = body;
    panel.querySelector('.trying').textContent = trying ? 'Looking for BunnyHop at ' + trying : '';
    panel.hidden = false;
  }

  async function round() {
    timer = null;
    if (!lost) return;
    for (const url of candidates()) {
      if (!lost) return;
      render(url.replace(/\/$/, ''));
      if (await probe(url)) {
        if (url === here()) { render(''); break; }  // same address: the websocket reconnects by itself
        location.href = url;
        return;
      }
    }
    if (lost) timer = setTimeout(round, ROUND_MS);
  }

  // Only when served by BunnyHop itself (not by tools/serial_bridge.py or
  // the mock on localhost, which must never be moved away from).
  const servedByBunnyHop = () => onAp() || location.hostname === 'bunnyhop.local' || location.hostname === known.lanIp;

  function onClose() {
    if (lost || !servedByBunnyHop()) return;
    lost = true;
    // a brief blip shouldn't flash a panel: give the websocket a moment first
    timer = setTimeout(round, 1500);
  }

  function onOpen() {
    lost = false;
    if (timer) clearTimeout(timer);
    timer = null;
    panel.hidden = true;
  }

  function init(r) {
    rabbit = r;
    panel = document.getElementById('moving');
    rabbit.on('hello', (m) => {
      remember(m.wifi);
      if (expected && expected.kind === 'pair' && m.wifi && m.wifi.lastPair) {
        Events.add('Pairing: ' + m.wifi.lastPair);
        expected = null;
      }
    });
    rabbit.on('network', (m) => { known.ap = m.ap || known.ap; known.private = !!m.private; if (m.lan) known.lan = m.lan; });
    rabbit.on('lan', (m) => { if (m.state === 'connected' && m.ip) known.lanIp = m.ip; });
    rabbit.on('pairing', (m) => { if (m.step === 'done' && expected && expected.kind === 'pair') expected = null; });
    rabbit.on('open', onOpen);
    rabbit.on('close', onClose);
  }

  return { init, expect, remember, known: () => known };
})();
