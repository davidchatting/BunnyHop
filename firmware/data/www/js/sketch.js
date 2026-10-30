// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * sketch.js - main entry: connects to the bridge, starts the UI modules and
 * draws the live rabbit (p5.js instance mode).
 *
 * The drawing shows the bridge's view of the rabbit: ears at their last
 * reported/commanded position, LEDs in their last commanded colour (the
 * rabbit can't report LED colours). Click an LED to paint it, drag an ear
 * to move it.
 */
const rabbit = new RabbitClient();
Events.init(rabbit);
Controls.init(rabbit);
Playground.init(rabbit);

new p5((p) => {
  const W = 360, H = 400;
  // The drawing faces the viewer, so the rabbit's left is on screen right.
  // LED positions: bottom, left, middle, right, nose. Ears: left, right.
  const LED_POS = [[180, 372], [230, 285], [180, 290], [130, 285], [180, 168]];
  const EAR_PIVOT = [[210, 120], [150, 120]];
  let shown = [0, 0];      // ear positions being drawn (eased)
  let dragging = -1;
  let pulse = 0;           // flash on incoming events
  let pulseText = '';

  function earTargets() {
    const s = rabbit.state;
    if (!s) return [0, 0];
    const e = s.ears;
    const pick = (rep, cmd) => (rep >= 0 ? rep : cmd >= 0 ? cmd : 0);
    return [pick(e.left, e.cmdLeft), pick(e.right, e.cmdRight)];
  }

  // Ear position 0..16 maps onto ~a full turn; 0 = straight up.
  const earAngle = (pos) => (pos / 17) * p.TWO_PI;

  p.setup = () => {
    const c = p.createCanvas(W, H);
    c.parent('rabbit-view');
    rabbit.on('ears', (m) => { shown = [m.left, m.right]; flash('ears moved'); });
    rabbit.on('button', (m) => flash(m.clicks === 2 ? 'double click' : 'click'));
    rabbit.on('rfid', (m) => flash('RFID ' + m.tag));
    rabbit.on('recording', () => flash('recording'));
  };

  function flash(txt) {
    pulse = 255;
    pulseText = txt;
  }

  function drawEar(i, pos) {
    const [x, y] = EAR_PIVOT[i];
    p.push();
    p.translate(x, y);
    // ears fall outwards: the rabbit's left (screen right) turns clockwise
    p.rotate(i === 0 ? earAngle(pos) * 0.5 : -earAngle(pos) * 0.5);
    p.stroke(200);
    p.strokeWeight(2);
    p.fill(dragging === i ? '#fff4e8' : 255);
    p.rect(-17, -110, 34, 115, 17);
    p.noStroke();
    p.fill(245, 220, 215);
    p.rect(-8, -95, 16, 85, 8);
    p.pop();
  }

  p.draw = () => {
    const s = rabbit.state;
    const live = s && s.connected;
    p.background(live ? '#f7f3ea' : '#ece8df');

    // ease ears toward their targets unless being dragged
    const t = earTargets();
    for (let i = 0; i < 2; i++) if (dragging !== i) shown[i] += (t[i] - shown[i]) * 0.15;
    drawEar(0, shown[0]);
    drawEar(1, shown[1]);

    // body
    p.stroke(210);
    p.strokeWeight(2);
    p.fill(255);
    p.beginShape();
    p.vertex(120, 130);
    p.bezierVertex(60, 260, 70, 360, 70, 380);
    p.vertex(290, 380);
    p.bezierVertex(290, 360, 300, 260, 240, 130);
    p.bezierVertex(220, 95, 140, 95, 120, 130);
    p.endShape(p.CLOSE);

    // LEDs with glow
    const cols = (s && s.leds) || [];
    LED_POS.forEach(([x, y], i) => {
      const c = p.color(cols[i] || Controls.leds[i] || '#000000');
      const lit = p.red(c) + p.green(c) + p.blue(c) > 20;
      p.noStroke();
      if (lit) {
        for (let r = 46; r > 20; r -= 6) {
          c.setAlpha(30);
          p.fill(c);
          p.circle(x, y, r);
        }
        c.setAlpha(255);
      }
      p.fill(lit ? c : p.color(235));
      p.stroke(lit ? 255 : 210);
      p.strokeWeight(1);
      p.circle(x, y, i === 0 ? 28 : 24);
    });

    // face
    p.noStroke();
    p.fill(40);
    p.ellipse(158, 150, 9, 13);
    p.ellipse(202, 150, 9, 13);
    p.fill(60);
    p.ellipse(180, 333, 6, 14); // button-ish mark like the real one

    // status overlay
    p.fill(live ? '#3a9d5d' : '#c8453a');
    p.textSize(12);
    p.textAlign(p.LEFT, p.TOP);
    p.text(live ? 'connected' : s ? (s.phase === 'idle' ? 'not connected' : s.phase + '…') : 'no bridge', 8, 8);
    if (s && s.asleep) { p.fill(90); p.text('asleep (zZz)', 8, 24); }

    if (pulse > 1) {
      p.fill(232, 102, 42, pulse);
      p.textAlign(p.CENTER, p.TOP);
      p.textSize(15);
      p.text(pulseText, W / 2, 40);
      pulse *= 0.96;
    }
  };

  function hitLed(mx, my) {
    return LED_POS.findIndex(([x, y]) => p.dist(mx, my, x, y) < 18);
  }

  function hitEar(mx, my) {
    for (let i = 0; i < 2; i++) {
      const [x, y] = EAR_PIVOT[i];
      if (p.dist(mx, my, x, y - 60) < 55) return i;
    }
    return -1;
  }

  // p5 already maps mouse coordinates through any CSS scaling of the canvas
  const scaled = () => [p.mouseX, p.mouseY];

  p.mousePressed = () => {
    const [mx, my] = scaled();
    if (mx < 0 || my < 0 || mx > W || my > H) return;
    const led = hitLed(mx, my);
    if (led >= 0) {
      Controls.setLedLocal(led, document.getElementById('led-brush').value);
      Controls.sendLeds(false);
      return false;
    }
    dragging = hitEar(mx, my);
    if (dragging >= 0) return false;
  };

  p.mouseDragged = () => {
    if (dragging < 0) return;
    const [mx, my] = scaled();
    const [x, y] = EAR_PIVOT[dragging];
    let a = Math.atan2(mx - x, -(my - y)); // 0 = up
    if (dragging === 1) a = -a;
    const pos = Math.round(p.constrain((a / p.TWO_PI) * 2 * 17, 0, 16));
    shown[dragging] = pos;
    const l = dragging === 0 ? pos : -1;
    const r = dragging === 1 ? pos : -1;
    Controls.setEarSliders(l, r);
    rabbit.ears(l, r, { throttle: true });
    return false;
  };

  p.mouseReleased = () => {
    if (dragging >= 0) {
      const l = dragging === 0 ? Math.round(shown[0]) : -1;
      const r = dragging === 1 ? Math.round(shown[1]) : -1;
      rabbit.ears(l, r).catch(() => {});
    }
    dragging = -1;
  };
}, document.getElementById('rabbit-view'));
