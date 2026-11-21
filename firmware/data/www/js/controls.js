// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * controls.js - buttons and pickers that send commands to the rabbit:
 * LED colour picker (opened from the drawing), sounds, quick actions, pairing.
 */
const Controls = (() => {
  const $ = (id) => document.getElementById(id);
  let rabbit;

  const report = (p) => p && p.catch && p.catch((e) => Events.add('⚠ ' + e.message));

  // ---- LEDs: tapping a light on the drawing shows a row of swatches
  // (plain buttons - reliable on every device, unlike opening the native
  // colour picker from script, which iPad Safari tends to block)
  const SWATCHES = ['#ff0000', '#ff7a00', '#ffd400', '#00d000', '#00c8ff',
    '#0040ff', '#a000ff', '#ff00a0', '#ffffff'];
  let pickingLed = -1;

  function closePalette() {
    $('palette').hidden = true;
    pickingLed = -1;
  }

  // x, y: page coordinates of the tapped light
  function pickLedColor(i, current, x, y) {
    pickingLed = i;
    const pal = $('palette');
    $('led-custom').value = current && current !== '#000000' ? current : '#ffffff';
    pal.hidden = false;
    const w = pal.offsetWidth;
    pal.style.left = Math.max(8, Math.min(document.documentElement.clientWidth - w - 8, x - w / 2)) + 'px';
    pal.style.top = y + 24 + 'px';
  }

  function setLed(color) {
    if (pickingLed >= 0) report(rabbit.led(pickingLed, color));
  }

  function initLeds() {
    const box = $('palette').querySelector('.swatches');
    for (const c of SWATCHES) {
      const b = document.createElement('button');
      b.style.background = c;
      b.title = c;
      b.onclick = () => { setLed(c); closePalette(); };
      box.appendChild(b);
    }
    const off = document.createElement('button');
    off.className = 'off';
    off.textContent = 'off';
    off.onclick = () => { setLed('#000000'); closePalette(); };
    box.appendChild(off);
    $('led-custom').addEventListener('change', (e) => { setLed(e.target.value); closePalette(); });
    // tap anywhere else to close
    document.addEventListener('pointerdown', (e) => {
      if (!$('palette').hidden && !e.target.closest('#palette') && !e.target.closest('#rabbit-view')) closePalette();
    });
    $('lights-off').onclick = () => report(rabbit.leds(['#000', '#000', '#000', '#000', '#000']));
    $('ears-up').onclick = () => report(rabbit.ears(0, 0));
  }

  // ---- sounds
  async function refreshSounds() {
    let files = [];
    try { files = await (await fetch('/api/audio')).json(); } catch (e) { /* offline */ }
    const box = $('sounds');
    box.innerHTML = '';
    if (!files.length) {
      box.innerHTML = '<span class="empty">No sounds yet - add an MP3.</span>';
      return;
    }
    for (const f of files) {
      const b = document.createElement('button');
      b.textContent = '▶ ' + f.name.replace(/\.(mp3|wav)$/i, '').replace(/[_-]/g, ' ');
      b.title = 'Play on the rabbit';
      b.onclick = () => report(rabbit.play(f.url));
      box.appendChild(b);
    }
  }

  function initSounds() {
    $('sound-file').onchange = async (e) => {
      const file = e.target.files[0];
      if (!file) return;
      const fd = new FormData();
      fd.append('file', file, file.name);
      Events.add('Adding ' + file.name + '…');
      try {
        await fetch('/api/audio', { method: 'POST', body: fd });
      } catch (err) {
        Events.add('⚠ upload failed');
      }
      e.target.value = '';
      refreshSounds();
    };
    refreshSounds();
  }

  // ---- home Wi-Fi
  function initLan() {
    $('lan-scan').onclick = () => {
      $('lan-scan').textContent = 'Scanning…';
      report(rabbit.send({ cmd: 'lanScan' }));
    };
    rabbit.on('lanScan', (m) => {
      $('lan-scan').textContent = 'Scan';
      const dl = $('lan-networks');
      dl.innerHTML = '';
      for (const n of (m.networks || []).sort((a, b) => b.rssi - a.rssi)) {
        const o = document.createElement('option');
        o.value = n.ssid;
        o.label = n.open ? 'open' : '';
        dl.appendChild(o);
      }
      $('lan-ssid').focus();
    });
    $('lan-form').onsubmit = (e) => {
      e.preventDefault();
      const ssid = $('lan-ssid').value.trim();
      if (!ssid) return;
      const onRabbitNet = location.hostname === '192.168.4.1';
      if (onRabbitNet && !confirm('Once BunnyHop joins ' + ssid + ', this network becomes rabbit-only and ' +
        'this device will be disconnected. Reconnect to ' + ssid + ' and open http://bunnyhop.local/. Continue?')) return;
      report(rabbit.send({ cmd: 'lan', ssid, password: $('lan-pass').value }));
      $('lan-pass').value = '';
      Events.add('Joining ' + ssid + '…');
    };
    $('lan-forget').onclick = () => {
      if (!confirm('Forget the home Wi-Fi? The rabbit\'s network will become open again.')) return;
      report(rabbit.send({ cmd: 'lan', ssid: '' }));
      $('lan-ssid').value = '';
    };
  }

  function init(r) {
    rabbit = r;
    initLeds();
    initSounds();
    initLan();
    $('pair').onclick = () => {
      if (confirm('Look for a rabbit in pairing mode (blue lights) and connect it to this bridge?')) report(rabbit.pair());
    };
  }

  return { init, pickLedColor };
})();
