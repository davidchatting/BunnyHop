/*
 * controls.js - interface components that send commands to the rabbit
 * (ears, LEDs, audio, manual/raw commands, pairing).
 */
const Controls = (() => {
  const $ = (id) => document.getElementById(id);
  let rabbit;
  // Shared with the rabbit view so painting LEDs there updates pickers here
  const leds = ['#000000', '#000000', '#000000', '#000000', '#000000'];

  function reply(promise) {
    promise
      .then((r) => { $('cmd-reply').textContent = JSON.stringify(r, null, 1); })
      .catch((e) => {
        $('cmd-reply').textContent = 'error: ' + e.message;
        Events.log('ui', e.message, 'err');
      });
    return promise;
  }

  // ---------------- ears ----------------
  function setEarSliders(l, r) {
    if (l >= 0) { $('ear-left').value = l; $('ear-left-v').textContent = l; }
    if (r >= 0) { $('ear-right').value = r; $('ear-right-v').textContent = r; }
  }

  function sendEars(throttle) {
    const l = +$('ear-left').value;
    const r = +$('ear-right').value;
    setEarSliders(l, r);
    if (throttle) rabbit.ears(l, r, { throttle: true });
    else reply(rabbit.ears(l, r));
  }

  function initEars() {
    for (const id of ['ear-left', 'ear-right']) {
      $(id).addEventListener('input', () => {
        setEarSliders(+$('ear-left').value, +$('ear-right').value);
        if ($('ear-live').checked) sendEars(true);
      });
      $(id).addEventListener('change', () => sendEars(false));
    }
    document.querySelectorAll('[data-ears]').forEach((b) => {
      b.onclick = () => {
        const [l, r] = b.dataset.ears.split(',').map(Number);
        setEarSliders(l, r);
        sendEars(false);
      };
    });
    rabbit.on('ears', (m) => setEarSliders(m.left, m.right));
  }

  // ---------------- LEDs ----------------
  function setLedLocal(i, hex) {
    leds[i] = hex;
    const p = document.querySelector(`#led-pickers input[data-led="${i}"]`);
    if (p) p.value = hex;
  }

  function sendLeds(throttle) {
    if (throttle) rabbit.leds(leds, { throttle: true });
    else reply(rabbit.leds(leds));
  }

  function initLeds() {
    const grid = $('led-pickers');
    RabbitClient.LEDS.forEach((name, i) => {
      const cell = document.createElement('label');
      cell.innerHTML = `<input type="color" data-led="${i}" value="#000000">${name}`;
      grid.appendChild(cell);
      cell.querySelector('input').addEventListener('input', (e) => {
        leds[i] = e.target.value;
        sendLeds(true);
      });
    });
    $('led-all').onclick = () => {
      for (let i = 0; i < 5; i++) setLedLocal(i, $('led-brush').value);
      sendLeds(false);
    };
    $('led-off').onclick = () => {
      for (let i = 0; i < 5; i++) setLedLocal(i, '#000000');
      sendLeds(false);
    };
    $('led-send').onclick = () => sendLeds(false);
    $('nose-mode').onchange = (e) => reply(rabbit.ambient([[8, +e.target.value]]));
    // follow LED changes made by sketches and other clients
    const follow = (m) => {
      if (document.activeElement && document.activeElement.closest('#led-pickers')) return;
      (m.state.leds || []).forEach((c, i) => setLedLocal(i, c));
    };
    rabbit.on('hello', follow);
    rabbit.on('state', follow);
  }

  // ---------------- audio ----------------
  async function refreshAudio() {
    let files = [];
    try { files = await (await fetch('/api/audio')).json(); } catch (e) { /* offline */ }
    const list = $('audio-list');
    list.innerHTML = files.length ? '' : '<div class="muted">No audio files yet - upload an MP3.</div>';
    for (const f of files) {
      const row = document.createElement('div');
      row.className = 'item';
      row.innerHTML = `<span class="mono"></span><span class="row"><button data-a="play">▶ rabbit</button>
        <button data-a="local">▶ here</button><button data-a="del">✕</button></span>`;
      row.querySelector('.mono').textContent = `${f.name} (${Math.round(f.bytes / 1024)} kB)`;
      row.querySelector('[data-a=play]').onclick = () => reply(rabbit.play(f.url));
      row.querySelector('[data-a=local]').onclick = () => new Audio(f.url).play();
      row.querySelector('[data-a=del]').onclick = async () => {
        if (!confirm('Delete ' + f.name + '?')) return;
        await fetch('/api/audio?name=' + encodeURIComponent(f.name), { method: 'DELETE' });
        refreshAudio();
      };
      list.appendChild(row);
    }
  }

  function initAudio() {
    $('audio-upload').onsubmit = async (e) => {
      e.preventDefault();
      const file = $('audio-file').files[0];
      if (!file) return;
      const fd = new FormData();
      fd.append('file', file, file.name);
      Events.log('ui', `uploading ${file.name} (${file.size} bytes)…`);
      try {
        await fetch('/api/audio', { method: 'POST', body: fd });
        Events.log('ui', 'upload done');
      } catch (err) {
        Events.log('ui', 'upload failed: ' + err.message, 'err');
      }
      refreshAudio();
    };
    $('audio-play').onclick = () => reply(rabbit.play($('audio-url').value.trim()));
    refreshAudio();
  }

  // ---------------- commands ----------------
  function initCommands() {
    document.querySelectorAll('[data-cmd]').forEach((b) => {
      b.onclick = () => reply(rabbit.send(JSON.parse(b.dataset.cmd)));
    });
    $('cmd-send').onclick = () => {
      let c;
      try { c = JSON.parse($('cmd-json').value); } catch (e) {
        $('cmd-reply').textContent = 'invalid JSON: ' + e.message;
        return;
      }
      reply(rabbit.send(c));
    };
    $('program-send').onclick = () => reply(rabbit.program($('cmd-program').value));
    $('raw-send').onclick = () => reply(rabbit.raw($('cmd-raw').value));
    $('wifi-pair').onclick = () => {
      if (confirm('Scan for a rabbit in setup mode (blue LEDs) and provision it?')) reply(rabbit.pair());
    };
    $('wifi-refresh').onclick = () =>
      rabbit.getWifi().then((w) => { rabbit.wifi = w; Events.diag(); });
  }

  function init(r) {
    rabbit = r;
    initEars();
    initLeds();
    initAudio();
    initCommands();
  }

  return { init, leds, setLedLocal, sendLeds, setEarSliders, sendEars };
})();
