/*
 * events.js - incoming events from the bridge -> page UI
 * (status badges, event log, RFID list, diagnostics).
 */
const Events = (() => {
  const $ = (id) => document.getElementById(id);
  const tags = new Map(); // tag -> {count, last}
  let rabbit;

  // log categories hidden unless "protocol detail" is ticked
  const VERBOSE = new Set(['xmpp>', 'xmpp<', 'net', 'api']);

  function log(cat, msg, cls) {
    if (VERBOSE.has(cat) && !$('log-verbose').checked) return;
    const el = $('log');
    const line = document.createElement('div');
    line.className = 'cat-' + (cls || cat);
    const t = new Date().toLocaleTimeString();
    line.textContent = `${t} ${cat.padEnd(6)} ${msg}`;
    const atBottom = el.scrollTop + el.clientHeight >= el.scrollHeight - 20;
    el.appendChild(line);
    while (el.childNodes.length > 500) el.removeChild(el.firstChild);
    if (atBottom) el.scrollTop = el.scrollHeight;
  }

  function badges() {
    const b = $('badge-bridge');
    b.className = 'badge ' + (rabbit.connected ? 'on' : 'off');
    b.textContent = rabbit.connected ? 'bridge' : 'bridge offline';
    const r = $('badge-rabbit');
    const s = rabbit.state;
    if (!rabbit.connected || !s) {
      r.className = 'badge off';
      r.textContent = 'rabbit: ?';
    } else if (s.connected) {
      r.className = 'badge on';
      r.textContent = 'rabbit: ' + (s.asleep ? 'asleep' : 'ready');
    } else {
      r.className = 'badge ' + (s.phase === 'idle' ? 'off' : 'mid');
      r.textContent = 'rabbit: ' + (s.phase === 'idle' ? 'not connected' : s.phase + '…');
    }
    $('rabbit-id').textContent = s && s.sn ? s.sn.replace(/(..)(?!$)/g, '$1:') : '';
  }

  function diag() {
    const s = rabbit.state || {};
    const w = rabbit.wifi || {};
    const rows = {
      'AP SSID': w.ap,
      'Web clients': w.apClients,
      'Rabbit MAC': s.sn,
      'Rabbit IP': s.ip,
      'Bootcode rev': s.bootcodeRev,
      'XMPP resource': s.resource,
      'Phase': s.phase,
      'Last seen': s.lastSeenMsAgo >= 0 ? (s.lastSeenMsAgo / 1000).toFixed(1) + ' s ago' : 'never',
      'Setup SSID': w.setupSsid,
      'Last pairing': w.lastPair,
      'Setup page': w.setupInfo,
    };
    $('diag').innerHTML = '';
    for (const [k, v] of Object.entries(rows)) {
      const dt = document.createElement('dt');
      dt.textContent = k;
      const dd = document.createElement('dd');
      dd.textContent = v === undefined || v === '' ? '–' : v;
      $('diag').append(dt, dd);
    }
  }

  function rfid(tag) {
    const cur = $('rfid-current');
    cur.textContent = tag;
    cur.classList.remove('flash');
    void cur.offsetWidth; // restart animation
    cur.classList.add('flash');
    const e = tags.get(tag) || { count: 0 };
    e.count++;
    e.last = new Date();
    tags.set(tag, e);
    const list = $('rfid-list');
    list.innerHTML = '';
    [...tags.entries()]
      .sort((a, b) => b[1].last - a[1].last)
      .forEach(([t, v]) => {
        const li = document.createElement('li');
        li.innerHTML = `<span class="mono"></span><span class="muted"></span>`;
        li.children[0].textContent = t;
        li.children[1].textContent = `×${v.count} · ${v.last.toLocaleTimeString()}`;
        list.appendChild(li);
      });
  }

  function init(r) {
    rabbit = r;
    rabbit.on('open', () => { log('ui', 'connected to bridge'); badges(); });
    rabbit.on('close', () => { log('ui', 'bridge connection lost - reconnecting', 'err'); badges(); });
    rabbit.on('hello', (m) => {
      $('badge-clients').textContent = m.clients + (m.clients === 1 ? ' client' : ' clients');
      badges();
      diag();
      rabbit.getLog().then((lines) => lines.forEach((l) => log(l.cat, l.msg))).catch(() => {});
    });
    rabbit.on('state', (m) => {
      $('badge-clients').textContent = m.clients + (m.clients === 1 ? ' client' : ' clients');
      badges();
      diag();
      const e = m.state.ears;
      $('ear-reported').textContent = e.left >= 0 ? `L ${e.left} · R ${e.right}` : '–';
      if (m.state.lastRecording) $('rec-last').innerHTML = `<a href="${m.state.lastRecording}" target="_blank">${m.state.lastRecording}</a>`;
    });
    rabbit.on('log', (m) => log(m.cat, m.msg));
    rabbit.on('connection', badges);
    rabbit.on('phase', badges);
    rabbit.on('rfid', (m) => rfid(m.tag));
    rabbit.on('ears', (m) => { $('ear-reported').textContent = `L ${m.left} · R ${m.right}`; });
    rabbit.on('recording', (m) => {
      $('rec-last').innerHTML = `<a href="${m.url}" target="_blank">${m.url}</a> (${m.bytes} bytes)`;
    });
    rabbit.on('pairing', () => rabbit.getWifi().then((w) => { rabbit.wifi = w; diag(); }));
    $('log-clear').onclick = () => { $('log').innerHTML = ''; };
    badges();
  }

  return { init, log, diag };
})();
