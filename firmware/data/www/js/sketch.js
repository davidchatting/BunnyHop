// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * sketch.js - main entry: connects to the bridge, starts the UI modules and
 * draws the rabbit (p5.js), which is also the main control:
 *   tap a light  -> colour picker for that LED
 *   drag an ear  -> moves that ear
 * The drawing faces you, so the rabbit's left is on the right of the screen.
 */
const rabbit = new RabbitClient();
Events.init(rabbit);
Controls.init(rabbit);

new p5((p) => {
  const W = 360, H = 420;
  // LED order: bottom, left, middle, right, nose (rabbit's own left/right)
  const LED_POS = [[180, 392], [232, 300], [180, 306], [128, 300], [180, 186]];
  const EAR_PIVOT = [[212, 140], [148, 140]];  // rabbit's left, right
  const EAR_LEN = 120;
  const target = [0, 0];   // ear positions 0..16 to show
  const shown = [0, 0];    // eased
  let leds = ['#000000', '#000000', '#000000', '#000000', '#000000'];
  let dragging = -1;
  let localUntil = 0;      // ignore bridge ear state briefly after our own drag

  // 0 = up, 8 = horizontal (outwards), 16 = down - as seen on the real rabbit
  const earAngle = (i, pos) => (i === 0 ? 1 : -1) * (pos / 16) * Math.PI;

  function earTip(i, pos) {
    const [x, y] = EAR_PIVOT[i];
    const a = earAngle(i, pos);
    return [x + Math.sin(a) * EAR_LEN, y - Math.cos(a) * EAR_LEN];
  }

  function fromState(s) {
    if (!s) return;
    const e = s.ears || {};
    if (dragging < 0 && Date.now() > localUntil) {
      target[0] = e.left >= 0 ? e.left : Math.max(0, e.cmdLeft);
      target[1] = e.right >= 0 ? e.right : Math.max(0, e.cmdRight);
    }
    if (s.leds) leds = s.leds.slice();
  }

  p.setup = () => {
    p.createCanvas(W, H).parent('rabbit-view');
    attachInput();
    rabbit.on('hello', (m) => fromState(m.state));
    rabbit.on('state', (m) => fromState(m.state));
    rabbit.on('ears', (m) => { if (dragging < 0) { target[0] = m.left; target[1] = m.right; } });
  };

  function drawEar(i) {
    const [x, y] = EAR_PIVOT[i];
    p.push();
    p.translate(x, y);
    p.rotate(earAngle(i, shown[i]));
    p.stroke(205);
    p.strokeWeight(2);
    p.fill(dragging === i ? '#fff1e6' : 255);
    p.rect(-18, -EAR_LEN, 36, EAR_LEN + 12, 18);
    p.noStroke();
    p.fill(246, 224, 218);
    p.rect(-8, -EAR_LEN + 14, 16, EAR_LEN - 28, 8);
    p.pop();
  }

  p.draw = () => {
    const live = rabbit.rabbitConnected;
    p.clear();
    for (let i = 0; i < 2; i++) shown[i] += (target[i] - shown[i]) * 0.2;
    drawEar(0);
    drawEar(1);

    // body
    p.stroke(210);
    p.strokeWeight(2);
    p.fill(255);
    p.beginShape();
    p.vertex(122, 150);
    p.bezierVertex(62, 280, 72, 380, 72, 400);
    p.vertex(288, 400);
    p.bezierVertex(288, 380, 298, 280, 238, 150);
    p.bezierVertex(218, 112, 142, 112, 122, 150);
    p.endShape(p.CLOSE);

    // eyes and the button mark
    p.noStroke();
    p.fill(40);
    p.ellipse(158, 168, 9, 13);
    p.ellipse(202, 168, 9, 13);
    p.ellipse(180, 352, 6, 14);

    // lights
    LED_POS.forEach(([x, y], i) => {
      const c = p.color(leds[i] || '#000000');
      const lit = p.red(c) + p.green(c) + p.blue(c) > 30;
      p.noStroke();
      if (lit) {
        for (let r = 52; r > 26; r -= 6) {
          c.setAlpha(28);
          p.fill(c);
          p.circle(x, y, r);
        }
        c.setAlpha(255);
      }
      p.fill(lit ? c : p.color(238));
      p.stroke(lit ? 255 : 205);
      p.strokeWeight(1.5);
      p.circle(x, y, i === 0 ? 30 : 26);
    });

    if (!live) {
      p.noStroke();
      p.fill(244, 241, 234, 170);
      p.rect(0, 0, W, H);
    }
  };

  function hitLed(x, y) {
    return LED_POS.findIndex(([lx, ly]) => p.dist(x, y, lx, ly) < 22);
  }

  // distance from the pointer to the ear's centre line
  function hitEar(x, y) {
    let best = -1, bestD = 26;
    for (let i = 0; i < 2; i++) {
      const [ax, ay] = EAR_PIVOT[i];
      const [bx, by] = earTip(i, shown[i]);
      const t = p.constrain(((x - ax) * (bx - ax) + (y - ay) * (by - ay)) / (EAR_LEN * EAR_LEN), 0, 1);
      const d = p.dist(x, y, ax + t * (bx - ax), ay + t * (by - ay));
      if (d < bestD) { best = i; bestD = d; }
    }
    return best;
  }

  function earFromPointer(i, x, y) {
    const [ax, ay] = EAR_PIVOT[i];
    const outward = (i === 0 ? 1 : -1) * (x - ax);
    let a = Math.atan2(outward, -(y - ay));     // 0 = up, PI/2 = outwards
    if (a < 0) a = 0;                           // can't go past upright inwards
    return Math.round(p.constrain((a / Math.PI) * 16, 0, 16));
  }

  // Input uses native Pointer Events (finger, pencil and mouse alike) rather
  // than p5's mouse/touch mapping, which doesn't fire on iPad Safari. The
  // colour picker opens from 'click', which Safari counts as a user action.
  function canvasPoint(e) {
    const r = p.canvas.getBoundingClientRect();
    return [((e.clientX - r.left) * W) / r.width, ((e.clientY - r.top) * H) / r.height];
  }

  function attachInput() {
    const c = p.canvas;
    c.addEventListener('pointerdown', (e) => {
      if (!rabbit.rabbitConnected) return;
      const [x, y] = canvasPoint(e);
      if (hitLed(x, y) >= 0) return;  // handled on click
      dragging = hitEar(x, y);
      if (dragging >= 0) {
        c.setPointerCapture(e.pointerId);
        e.preventDefault();
      }
    });
    c.addEventListener('pointermove', (e) => {
      if (dragging < 0) return;
      const [x, y] = canvasPoint(e);
      const pos = earFromPointer(dragging, x, y);
      target[dragging] = pos;
      if (dragging === 0) rabbit.ears(pos, null, { throttle: true });
      else rabbit.ears(null, pos, { throttle: true });
      e.preventDefault();
    });
    const endDrag = () => {
      if (dragging < 0) return;
      const i = dragging;
      dragging = -1;
      const pos = target[i];
      localUntil = Date.now() + 2500;
      (i === 0 ? rabbit.ears(pos, null) : rabbit.ears(null, pos)).catch(() => {});
    };
    c.addEventListener('pointerup', endDrag);
    c.addEventListener('pointercancel', endDrag);
    c.addEventListener('click', (e) => {
      if (!rabbit.rabbitConnected) return;
      const [x, y] = canvasPoint(e);
      const led = hitLed(x, y);
      if (led >= 0) Controls.pickLedColor(led, leds[led]);
    });
  }
}, document.getElementById('rabbit-view'));
