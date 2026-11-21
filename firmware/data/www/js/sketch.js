// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * sketch.js - main entry: connects to the bridge, starts the UI modules and
 * draws the rabbit (js/rabbit-drawing.js, an SVG), which is also the main control:
 *   tap a light  -> colour picker for that LED
 *   drag an ear  -> moves that ear
 * The drawing faces you, so the rabbit's left is on the right of the screen.
 * While the rabbit isn't connected it is drawn as a dashed ghost, lights off.
 *
 * Ear positions 0-16 are one full turn: 0 up, 0-8 swinging forward out to the
 * drawing's flopped pose, 8-16 coming back up the back way (shown with grey
 * depth). In the drawing that is a value -90 (back) .. 0 (up) .. 90 (forward).
 */
const rabbit = new RabbitClient();
Events.init(rabbit);
Controls.init(rabbit);
Moving.init(rabbit);

(() => {
  const SIDES = ['left', 'right'];
  const view = document.getElementById('rabbit-view');
  const drawing = createRabbit(view, { ink: '#2b2a27', paper: '#fffdf8' });
  const svg = drawing.svg;

  const target = { left: 0, right: 0 };   // ear positions 0..16 to show
  const shown = { left: 0, right: 0 };    // eased, same scale
  let leds = ['#000000', '#000000', '#000000', '#000000', '#000000'];
  let dragging = null;                    // { side, back } while dragging an ear
  let localUntil = 0;                     // ignore bridge ear state briefly after our own drag

  // Position 0..16 -> drawing value: 0..8 forward 0..90, 8..16 back 90..0.
  const toValue = (pos) => (pos <= 8 ? (pos / 8) * 90 : -((16 - pos) / 8) * 90);

  function fromState(s) {
    if (!s) return;
    const e = s.ears || {};
    if (!dragging && Date.now() > localUntil) {
      target.left = e.left >= 0 ? e.left : Math.max(0, e.cmdLeft);
      target.right = e.right >= 0 ? e.right : Math.max(0, e.cmdRight);
    }
    if (s.leds) leds = s.leds.slice();
  }
  rabbit.on('hello', (m) => fromState(m.state));
  rabbit.on('state', (m) => fromState(m.state));
  rabbit.on('ears', (m) => { if (!dragging) { target.left = m.left; target.right = m.right; } });

  // Positions wrap (16 is up again, like 0), so ease the short way round.
  function ease(side) {
    let d = target[side] - shown[side];
    if (d > 8) d -= 16; else if (d < -8) d += 16;
    shown[side] = Math.abs(d) < 0.01 ? target[side] : (shown[side] + d * 0.2 + 16) % 16;
    if (target[side] === 16 && shown[side] < 0.01) shown[side] = 16;
  }

  let wasLive = null, shownLeds = '';
  function frame() {
    const live = rabbit.rabbitConnected;
    if (live !== wasLive) { drawing.setGhost(!live); wasLive = live; }
    const want = live ? leds : ['', '', '', '', ''];
    if (want.join() !== shownLeds) { drawing.setLeds(want); shownLeds = want.join(); }
    SIDES.forEach(ease);
    drawing.setEars({ left: toValue(shown.left), right: toValue(shown.right) });
    requestAnimationFrame(frame);
  }
  requestAnimationFrame(frame);

  // ---- input (native Pointer Events: finger, pencil and mouse alike) ----

  function svgPoint(e) {
    const pt = svg.createSVGPoint();
    pt.x = e.clientX; pt.y = e.clientY;
    return pt.matrixTransform(svg.getScreenCTM().inverse());
  }

  function hitLed(p) {
    return LEDS.findIndex(([x, y, r]) => Math.hypot(p.x - x, p.y - y) < r * 2.4);
  }

  function hitEar(e) {
    const g = e.target.closest && e.target.closest('#ear-left, #ear-right');
    return g ? g.id.slice(4) : null;
  }

  // How far out the pointer is from the ear's pivot, 0 (up) .. 8 (drawn pose).
  function outFromPointer(side, p) {
    const ear = RABBIT.ears[side];
    const [px, py] = ear.pivot;
    let a = (Math.atan2(ear.sign * (p.x - px), -(p.y - py)) * 180) / Math.PI;
    a = Math.min(Math.max(a, 0), ear.drawnAngle);
    return (a / ear.drawnAngle) * 8;
  }

  svg.style.touchAction = 'none';
  svg.addEventListener('pointerdown', (e) => {
    if (!rabbit.rabbitConnected) return;
    const side = hitEar(e);
    if (!side) return;
    // Keep going the way the ear already is; swinging all the way out flips
    // between the forward half (0-8) and the back half (8-16) of the turn.
    dragging = { side, back: target[side] > 8 };
    svg.style.cursor = 'grabbing';
    svg.setPointerCapture(e.pointerId);
    e.preventDefault();
  });
  // Cursor over the drawing: a hand over a light, grab over an ear (only
  // while the rabbit is connected and can be controlled).
  function hoverCursor(e) {
    let c = '';
    if (rabbit.rabbitConnected && e.pointerType === 'mouse') {
      if (hitEar(e)) c = 'grab';
      else if (hitLed(svgPoint(e)) >= 0) c = 'pointer';
    }
    if (svg.style.cursor !== c) svg.style.cursor = c;
  }
  svg.addEventListener('pointerleave', () => { if (!dragging) svg.style.cursor = ''; });

  svg.addEventListener('pointermove', (e) => {
    if (!dragging) return hoverCursor(e);
    const { side } = dragging;
    const out = outFromPointer(side, svgPoint(e));
    if (out >= 7.5) dragging.flip = true;
    else if (dragging.flip && out < 7) { dragging.back = !dragging.back; dragging.flip = false; }
    const pos = Math.round(dragging.back ? 16 - out : out);
    if (pos === target[side]) return;
    target[side] = pos;
    if (side === 'left') rabbit.ears(pos, null, { throttle: true });
    else rabbit.ears(null, pos, { throttle: true });
    e.preventDefault();
  });
  const endDrag = () => {
    if (!dragging) return;
    const { side } = dragging;
    dragging = null;
    svg.style.cursor = '';
    const pos = target[side];
    localUntil = Date.now() + 2500;
    (side === 'left' ? rabbit.ears(pos, null) : rabbit.ears(null, pos)).catch(() => {});
  };
  svg.addEventListener('pointerup', endDrag);
  svg.addEventListener('pointercancel', endDrag);

  svg.addEventListener('click', (e) => {
    if (!rabbit.rabbitConnected) return;
    const led = hitLed(svgPoint(e));
    if (led < 0) return;
    const [x, y] = LEDS[led];
    const pt = svg.createSVGPoint();
    pt.x = x; pt.y = y;
    const s = pt.matrixTransform(svg.getScreenCTM());
    Controls.pickLedColor(led, leds[led], window.scrollX + s.x, window.scrollY + s.y);
  });
})();
