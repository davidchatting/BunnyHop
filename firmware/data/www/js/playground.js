// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * playground.js - live-coding p5.js sketches wired to the rabbit.
 *
 * User code is ordinary global-mode p5 code. It runs inside
 * `with (p) { ... }` against a p5 instance, so `circle()`, `mouseX`,
 * `width` etc. work unqualified, and gets `rabbit` (a RabbitClient) in scope.
 */
const Playground = (() => {
  const $ = (id) => document.getElementById(id);
  const STORE_KEY = 'nabaztag-bridge.sketch';
  let rabbit;
  let instance = null;
  let unsubscribe = [];

  const EXAMPLES = {
    'Mouse → ears': [
      '// Move the mouse over the canvas: x drives the left ear, y the right.',
      'function setup() {',
      '  createCanvas(400, 240);',
      '}',
      '',
      'function draw() {',
      '  background(250, 245, 235);',
      '  const l = round(map(mouseX, 0, width, 0, 16, true));',
      '  const r = round(map(mouseY, 0, height, 0, 16, true));',
      '  fill(232, 102, 42); noStroke();',
      '  circle(mouseX, mouseY, 24);',
      '  fill(40); textSize(16);',
      "  text('left ' + l + '   right ' + r, 12, 24);",
      '  if (mouseIsPressed) rabbit.ears(l, r, { throttle: true });',
      "  text('hold the mouse button to send', 12, height - 12);",
      '}',
    ],
    'Rainbow LEDs': [
      '// A rainbow that runs smoothly on the rabbit itself: every 6 s the',
      '// sketch sends a 6-second choreography (one LED frame every 0.2 s).',
      '// Streaming colours instead is limited to ~1 update per 1.5 s, because',
      '// the rabbit blanks its LEDs while downloading each update.',
      'let on = true, lastSent = -1e9;',
      'const SECONDS = 6, STEP = 20;  // STEP in 10 ms units',
      'function setup() {',
      '  createCanvas(400, 120);',
      '  colorMode(HSB, 360, 100, 100);',
      '}',
      '',
      'function hueAt(t, i) { return (t * 60 + i * 50) % 360; }  // t in seconds',
      '',
      'function sendRainbow(t0) {',
      '  const steps = [];',
      '  for (let k = 0; k < SECONDS * 100 / STEP; k++) {',
      '    for (let i = 0; i < 5; i++) {',
      '      const c = color(hueAt(t0 + k * STEP / 100, i), 90, 100);',
      "      steps.push({ type: 'led', led: i, color: [red(c), green(c), blue(c)], delay: i === 0 && k > 0 ? 1 : 0 });",
      '    }',
      '  }',
      '  rabbit.chor(steps, STEP).catch(() => {});',
      '}',
      '',
      'function draw() {',
      '  background(0, 0, 97);',
      '  const t = millis() / 1000;',
      '  for (let i = 0; i < 5; i++) {',
      '    fill(hueAt(t, i), 90, 100); noStroke();',
      '    circle(40 + i * 80, 60, 50);',
      '  }',
      '  if (on && t - lastSent >= SECONDS) { lastSent = t; sendRainbow(t); }',
      "  fill(0, 0, 40); text(on ? 'click to stop' : 'click to start', 8, 112);",
      '}',
      '',
      'function mousePressed() { on = !on; lastSent = -1e9; }',
    ],
    'RFID colours': [
      '// Each RFID tag gets its own colour; show a tag to the rabbit.',
      "let last = 'show me a tag';",
      'let col;',
      'function setup() {',
      '  createCanvas(400, 160);',
      '  col = color(200);',
      '}',
      '',
      'function onRfid(e) {',
      '  last = e.tag;',
      '  randomSeed(parseInt(e.tag.slice(-6), 16));',
      '  col = color(random(255), random(255), random(255));',
      '  rabbit.leds([col, col, col, col, col]);',
      '  rabbit.ears(random(0, 16), random(0, 16));',
      '}',
      '',
      'function draw() {',
      '  background(col);',
      '  fill(0); textSize(20); textAlign(CENTER, CENTER);',
      '  text(last, width / 2, height / 2);',
      '}',
    ],
    'Button counter': [
      '// Counts clicks on the head button; double-click resets.',
      'let clicks = 0, flash = 0;',
      'function setup() { createCanvas(400, 160); }',
      '',
      'function onButton(e) {',
      '  if (e.clicks === 2) clicks = 0; else clicks++;',
      '  flash = 255;',
      "  rabbit.led(4, clicks % 2 ? '#ff0000' : '#0000ff');",
      '}',
      '',
      'function draw() {',
      '  background(255, 255 - flash * 0.5, 255 - flash);',
      '  flash *= 0.9;',
      '  fill(30); textSize(64); textAlign(CENTER, CENTER);',
      '  text(clicks, width / 2, height / 2);',
      '}',
    ],
    'Ear mirror': [
      '// Move the ears by hand: their positions draw on screen and set',
      '// the LED brightness.',
      'let l = 0, r = 0;',
      'function setup() { createCanvas(400, 200); angleMode(DEGREES); }',
      '',
      'function onEars(e) {',
      '  l = e.left; r = e.right;',
      '  const b = map(l + r, 0, 32, 0, 255);',
      '  const c = color(b, b * 0.6, 255 - b);',
      '  rabbit.leds([c, c, c, c, c]);',
      '}',
      '',
      'function ear(x, pos) {',
      '  push(); translate(x, 150); rotate(pos * 360 / 17);',
      '  fill(250); stroke(80); strokeWeight(2);',
      '  rect(-15, -110, 30, 110, 15);',
      '  pop();',
      '}',
      '',
      'function draw() {',
      '  background(235);',
      '  ear(140, l); ear(260, r);',
      "  noStroke(); fill(60); text('L ' + l + '  R ' + r, 10, 20);",
      '}',
    ],
  };

  function showError(e) {
    const el = $('pg-error');
    if (!e) { el.hidden = true; return; }
    el.hidden = false;
    el.textContent = String(e && e.stack ? e.message : e);
    console.error(e);
  }

  function stop() {
    unsubscribe.forEach((u) => u());
    unsubscribe = [];
    if (instance) instance.remove();
    instance = null;
  }

  function run() {
    stop();
    showError(null);
    const code = $('pg-code').value;
    localStorage.setItem(STORE_KEY, code);
    const P5_HOOKS = ['preload', 'setup', 'draw', 'mousePressed', 'mouseReleased', 'mouseDragged',
      'mouseMoved', 'keyPressed', 'keyReleased', 'windowResized', 'touchStarted', 'touchEnded'];
    const RABBIT_HOOKS = { onButton: 'button', onRfid: 'rfid', onEars: 'ears', onRecording: 'recording',
      onConnection: 'connection' };
    const names = P5_HOOKS.concat(Object.keys(RABBIT_HOOKS));
    let factory;
    try {
      // eslint-disable-next-line no-new-func
      factory = new Function('p', 'rabbit',
        'with (p) {\n' + code + '\n}\nreturn {' +
        names.map((n) => `${n}: typeof ${n} === 'function' ? ${n} : undefined`).join(',') + '};');
    } catch (e) {
      showError(e);
      return;
    }
    const guard = (fn) => function () {
      try { return fn.apply(this, arguments); } catch (e) { showError(e); if (instance) instance.noLoop(); }
    };
    instance = new p5((p) => {
      let hooks;
      try { hooks = factory(p, rabbit); } catch (e) { showError(e); return; }
      for (const n of P5_HOOKS) if (hooks[n]) p[n] = guard(hooks[n]);
      if (!hooks.setup) p.setup = () => p.createCanvas(400, 200);
      for (const [n, ev] of Object.entries(RABBIT_HOOKS)) {
        if (hooks[n]) unsubscribe.push(rabbit.on(ev, guard(hooks[n])));
      }
    }, $('pg-canvas'));
  }

  function init(r) {
    rabbit = r;
    const sel = $('pg-examples');
    sel.innerHTML = '<option value="">Examples…</option>' +
      Object.keys(EXAMPLES).map((k) => `<option>${k}</option>`).join('');
    sel.onchange = () => {
      if (!sel.value) return;
      $('pg-code').value = EXAMPLES[sel.value].join('\n') + '\n';
      sel.value = '';
      run();
    };
    $('pg-code').value = localStorage.getItem(STORE_KEY) || EXAMPLES['Mouse → ears'].join('\n') + '\n';
    $('pg-run').onclick = run;
    $('pg-stop').onclick = stop;
    $('pg-code').addEventListener('keydown', (e) => {
      if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') { e.preventDefault(); run(); }
      if (e.key === 'Tab') {
        e.preventDefault();
        const t = e.target, s = t.selectionStart;
        t.value = t.value.slice(0, s) + '  ' + t.value.slice(t.selectionEnd);
        t.selectionStart = t.selectionEnd = s + 2;
      }
    });
    run();
  }

  return { init, run, stop, EXAMPLES };
})();
