// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * controls.js - buttons and pickers that send commands to the rabbit:
 * LED colour picker (opened from the drawing), sounds, quick actions, pairing.
 */
const Controls = (() => {
  const $ = (id) => document.getElementById(id);
  let rabbit;

  const report = (p) => p && p.catch && p.catch((e) => Events.add('⚠ ' + e.message));

  // ---- LEDs: tapping a light on the drawing opens the native colour picker
  let pickingLed = -1;
  function pickLedColor(i, current) {
    pickingLed = i;
    const picker = $('led-picker');
    picker.value = current || '#ffffff';
    picker.click();
  }

  function initLeds() {
    const picker = $('led-picker');
    // live preview while dragging inside the picker, coalesced by the bridge
    picker.addEventListener('input', () => {
      if (pickingLed >= 0) report(rabbit.led(pickingLed, picker.value));
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

  function init(r) {
    rabbit = r;
    initLeds();
    initSounds();
    $('pair').onclick = () => {
      if (confirm('Look for a rabbit in pairing mode (blue lights) and connect it to this bridge?')) report(rabbit.pair());
    };
  }

  return { init, pickLedColor };
})();
