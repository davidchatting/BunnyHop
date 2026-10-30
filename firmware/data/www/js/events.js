// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * events.js - things the rabbit and bridge report -> connection status,
 * activity list and connection details.
 */
const Events = (() => {
  const $ = (id) => document.getElementById(id);
  let rabbit;

  function status() {
    const el = $('status');
    const s = rabbit.state;
    let cls = 'off', text = 'bridge not reachable';
    if (rabbit.connected && s) {
      if (s.connected) { cls = 'on'; text = s.asleep ? 'asleep' : 'connected'; }
      else if (s.phase && s.phase !== 'idle') { cls = 'mid'; text = 'rabbit starting…'; }
      else text = 'rabbit not connected';
    } else if (!rabbit.connected) text = 'connecting…';
    el.className = 'status ' + cls;
    el.textContent = text;
  }

  function add(text, link) {
    const list = $('activity');
    const empty = list.querySelector('.empty');
    if (empty) empty.remove();
    const li = document.createElement('li');
    li.className = 'new';
    const time = document.createElement('span');
    time.className = 'time';
    time.textContent = new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
    const what = document.createElement('span');
    what.textContent = text;
    li.append(time, what);
    if (link) {
      const b = document.createElement('button');
      b.textContent = '▶ play';
      b.onclick = () => new Audio(link).play();
      li.append(b);
    }
    list.prepend(li);
    while (list.children.length > 30) list.lastChild.remove();
  }

  function info() {
    const s = rabbit.state || {};
    const w = rabbit.wifi || {};
    const rows = {
      "Rabbit's network": w.ap ? w.ap + (w.private ? ' (hidden, rabbit only)' : ' (open)') : '',
      'Rabbit': s.sn ? s.sn.replace(/(..)(?!$)/g, '$1:') : '',
      'State': s.connected ? 'connected' : s.phase,
    };
    const dl = $('info');
    dl.innerHTML = '';
    for (const [k, v] of Object.entries(rows)) {
      const dt = document.createElement('dt');
      dt.textContent = k;
      const dd = document.createElement('dd');
      dd.textContent = v || '–';
      dl.append(dt, dd);
    }
    const lan = w.lan || {};
    const text = {
      unset: 'Not set up.',
      connecting: 'Connecting to ' + lan.ssid + '…',
      connected: 'Connected to ' + lan.ssid + ' (' + lan.ip + ') - http://' + lan.host + '/',
      unavailable: "Can't reach " + lan.ssid + ' - retrying every minute.',
    }[lan.state] || '';
    $('lan-status').textContent = text;
    if (lan.ssid && document.activeElement !== $('lan-ssid') && !$('lan-ssid').value) $('lan-ssid').value = lan.ssid;
  }

  // the bridge's Wi-Fi status isn't in the periodic state push; poll it
  function refreshWifi() {
    if (!rabbit.connected) return;
    rabbit.getWifi().then((w) => { rabbit.wifi = w; info(); status(); }).catch(() => {});
  }

  function init(r) {
    rabbit = r;
    for (const ev of ['open', 'close', 'hello', 'state', 'connection', 'phase']) rabbit.on(ev, () => { status(); info(); });
    rabbit.on('button', (m) => add(m.clicks === 2 ? 'Button double-clicked' : 'Button pressed'));
    rabbit.on('rfid', (m) => add('Tag ' + m.tag));
    rabbit.on('ears', (m) => add(`Ears moved by hand (left ${m.left}, right ${m.right})`));
    rabbit.on('recording', (m) => add('Recorded a message', m.url));
    rabbit.on('connection', (m) => add(m.connected ? 'Rabbit connected' : 'Rabbit disconnected'));
    rabbit.on('pairing', (m) => add(m.step === 'done' ? 'Rabbit paired' : 'Pairing with ' + (m.ssid || 'rabbit') + '…'));
    rabbit.on('lan', (m) => {
      add(m.state === 'connected' ? 'Joined home Wi-Fi (' + m.ip + ')' : "Home Wi-Fi isn't reachable");
      refreshWifi();
    });
    setInterval(refreshWifi, 5000);
    status();
  }

  return { init, add, refreshWifi };
})();
