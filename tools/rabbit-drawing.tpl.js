// Nabaztag character from network-actions-rabbits.pdf (2018), as a poseable SVG.
// Ears are rabbit-centric: "left" is the rabbit's own left (the viewer's right).
// Each ear has one control, -90..90:
//   0 is upright, +90 is fully forward, -90 fully back.
// From the front the ear swings outward with the size of the value, and at 90
// it is exactly the flopped ear drawn in the PDF (page 3 right ear, tilted
// 76.6 degrees; page 5 left ear, 65.6 degrees). In between it rotates
// proportionally about its base while the outline blends point by point from
// the upright shape to the drawn one. Forward and back look the same in
// outline; going back, a grey smudge shows how far the ear is behind its
// starting depth: none at 0, deepest at 45, and none again at 90.
// Ears are drawn in front of the body.
const RABBIT = __DATA__;
const SVGNS = 'http://www.w3.org/2000/svg';
// LEDs in BunnyHop order: bottom, left, middle, right, nose (rabbit's own left/right,
// so "left" is on the viewer's right). [x, y, radius] in drawing units.
const LEDS = [[5.5, 51.8, 0.7], [8.25, 47.6, 0.6], [5.55, 47.75, 0.6], [2.85, 47.6, 0.6], [5.95, 41.5, 0.5]];
const LED_NAMES = ['bottom', 'left', 'middle', 'right', 'nose'];

function earPath(ear, value) {
  const sm = Math.min(Math.max(value, 0), 90) / 90;    // 0 upright .. 1 the PDF's drawn pose
  const a = sm * ear.drawnAngle * ear.sign * Math.PI / 180, c = Math.cos(a), n = Math.sin(a);
  const [px, py] = ear.pivot;
  const turn = ([x, y]) => [px + (x - px) * c - (y - py) * n, py + (x - px) * n + (y - py) * c];
  const f = p => p[0].toFixed(3) + ' ' + p[1].toFixed(3);
  if (sm > 0.001 && sm < 0.999) {
    // In between: blend the evenly resampled outlines and draw them as a smooth
    // closed Catmull-Rom curve, so the ear has no kinks mid-morph.
    const P = ear.upS.map(([x, y], i) => turn([x + sm * (ear.drawnS[i][0] - x), y + sm * (ear.drawnS[i][1] - y)]));
    const N = P.length, at = i => P[(i + N) % N];
    let d = 'M' + f(P[0]);
    for (let i = 0; i < N; i++) {
      const p0 = at(i - 1), p1 = at(i), p2 = at(i + 1), p3 = at(i + 2);
      d += 'C' + f([p1[0] + (p2[0] - p0[0]) / 6, p1[1] + (p2[1] - p0[1]) / 6]) + ' ' +
        f([p2[0] - (p3[0] - p1[0]) / 6, p2[1] - (p3[1] - p1[1]) / 6]) + ' ' + f(p2);
    }
    return d + 'Z';
  }
  // At either end: the PDF's own curves, rotated.
  const pts = (sm < 0.5 ? ear.up : ear.drawnLocal).map(turn);
  let d = '', k = 0, cur = null;
  for (const op of ear.ops) {
    const m = op === 'c' ? 4 : 2, seg = pts.slice(k, k + m); k += m;
    if (!cur || Math.hypot(cur[0] - seg[0][0], cur[1] - seg[0][1]) > 1e-3) d += 'M' + f(seg[0]);
    d += (op === 'c' ? 'C' + seg.slice(1).map(f).join(' ') : 'L' + f(seg[1]));
    cur = seg[m - 1];
  }
  return d;
}

// Builds the rabbit inside `container`. Returns { svg, setEars({left, right}), setLeds([5 colours]), setLed(i, colour), setGhost(bool) }.
// A colour of null, '' or black turns that LED off. Each LED has a thin grey
// circle as a tap target; pass { targets: false } to leave them out.
function createRabbit(container, { ink = '#000', paper = '#fff', target = '#a8a59d', targets = true } = {}) {
  const svg = document.createElementNS(SVGNS, 'svg');
  svg.setAttribute('viewBox', '-10 22 31 33');
  svg.setAttribute('role', 'img');
  svg.setAttribute('aria-label', 'Nabaztag rabbit');
  const el = (tag, attrs) => {
    const e = document.createElementNS(SVGNS, tag);
    for (const k in attrs) e.setAttribute(k, attrs[k]);
    return e;
  };
  const line = { fill: 'none', stroke: ink, 'stroke-width': 0.2, 'stroke-linecap': 'round', 'stroke-linejoin': 'round' };
  const outline = el('g', { id: 'outline' });
  const defs = el('defs', {});
  // LEDs are pure blurred light, no hard edges: a wide halo and a soft core.
  const blur = (id, sd) => {
    const f = el('filter', { id, x: '-2', y: '-2', width: '5', height: '5' });
    f.append(el('feGaussianBlur', { stdDeviation: sd }));
    return f;
  };
  defs.append(blur('rabbit-glow', 0.5), blur('rabbit-core', 0.18), blur('rabbit-smudge', 0.4));
  // The body LEDs are clipped to the inside of the body.
  const bodyClip = el('clipPath', { id: 'rabbit-body-clip' });
  bodyClip.append(el('path', { d: RABBIT.shapes.body }));
  defs.append(bodyClip);
  svg.append(defs);
  // An ear that has gone back gets a grey smudge, the same soft style as the
  // LEDs: a blurred grey copy of the ear clipped to its outline, faded in with
  // depth. Layers: paper fill, smudge, outline.
  const ears = {};
  for (const side of ['right', 'left']) {
    const g = el('g', { id: 'ear-' + side });
    const clip = el('clipPath', { id: 'rabbit-ear-clip-' + side });
    const clipShape = el('path', {});
    clip.append(clipShape);
    defs.append(clip);
    const fill = el('path', { fill: paper });
    const smudge = el('path', { fill: ink, filter: 'url(#rabbit-smudge)', 'clip-path': `url(#rabbit-ear-clip-${side})`, opacity: 0 });
    const stroke = el('path', line);
    // a wider invisible edge, so a thin ear is easy to grab with a finger
    const grab = el('path', { fill: 'none', stroke: 'transparent', 'stroke-width': 2.5, 'pointer-events': 'stroke' });
    g.append(fill, smudge, stroke, grab);
    outline.append(g);
    ears[side] = { paths: [clipShape, fill, smudge, stroke, grab], smudge };
  }
  const head = el('g', { id: 'head' });
  const base = el('g', { id: 'base-light' });
  head.append(base);
  head.append(el('path', { id: 'body-fill', d: RABBIT.shapes.body, fill: paper }));
  const bodyLine = el('path', { id: 'body', d: RABBIT.shapes.body, ...line });
  const face = el('g', { id: 'face', fill: ink });
  face.append(el('path', { id: 'eyes', d: RABBIT.shapes.eyes }), el('path', { id: 'mouth', d: RABBIT.shapes.mouth }));
  // LEDs sit between the body fill and its outline, under the face ink, so the
  // nose glows under the drawn nose and the outline stays crisp over the light.
  const ledGroup = el('g', { id: 'leds', 'clip-path': 'url(#rabbit-body-clip)' });
  const leds = LEDS.map(([x, y, r], i) => {
    const g = el('g', { id: 'led-' + LED_NAMES[i] });
    // The bottom LED lights the surface under the rabbit: it spreads along the
    // bottom edge and sits behind the body, so only the light outside shows.
    const wide = i === 0 ? 4 : 1;
    const glow = el('ellipse', { cx: x, cy: y, rx: r * 1.8 * wide, ry: r * 1.8, filter: 'url(#rabbit-glow)', opacity: 0 });
    const dot = el('ellipse', { cx: x, cy: y, rx: r * 0.75 * wide, ry: r * 0.75, filter: 'url(#rabbit-core)', opacity: 0 });
    // The bottom LED is just the soft halo, no core.
    if (i === 0) g.append(glow); else g.append(glow, dot);
    (i === 0 ? base : ledGroup).append(g);
    return { glow, dot };
  });
  head.append(ledGroup, bodyLine, face);
  // Tap targets: a grey ring round each LED, over the face so the nose ring shows.
  if (targets) {
    const rings = el('g', { id: 'led-targets', fill: 'none', stroke: target, 'stroke-width': 0.12, 'stroke-dasharray': 'none', 'pointer-events': 'none' });
    LEDS.forEach(([x, y, r], i) => rings.append(el('circle', { id: 'target-' + LED_NAMES[i], cx: x, cy: y, r: r * 1.5 })));
    head.append(rings);
  }
  outline.prepend(head);  // before the ears, so the ears sit in front of the body
  svg.append(outline);
  container.append(svg);
  const pose = { left: 0, right: 0 };
  function setEars(p) {
    for (const side of ['left', 'right']) if (p[side] != null) {
      const v = pose[side] = Math.min(Math.max(p[side], -90), 90);
      const d = earPath(RABBIT.ears[side], Math.abs(v));
      for (const e of ears[side].paths) e.setAttribute('d', d);
      const depth = v < 0 ? Math.sin(-v * Math.PI / 90) : 0;  // 0 at 0 and 90, 1 at 45
      ears[side].smudge.setAttribute('opacity', (0.25 * depth).toFixed(3));
    }
  }
  const ledColours = [null, null, null, null, null];
  const isOff = c => !c || /^#?0{3}(0{3})?$/.test(c);
  function setLed(i, colour) {
    ledColours[i] = colour;
    const { glow, dot } = leds[i], off = isOff(colour);
    for (const e of [glow, dot]) e.setAttribute('fill', off ? 'none' : colour);
    glow.setAttribute('opacity', off ? 0 : i === 0 ? 0.9 : 0.6);  // the bottom halo is half hidden
    dot.setAttribute('opacity', off ? 0 : 0.95);
  }
  function setLeds(list) { list.forEach((c, i) => setLed(i, c)); }
  function setGhost(on) { outline.setAttribute('stroke-dasharray', on ? '2 2' : 'none'); }
  setEars({ left: 0, right: 0 });
  return { svg, setEars, setLed, setLeds, leds: ledColours, setGhost, pose, pivots: { left: RABBIT.ears.left.pivot, right: RABBIT.ears.right.pivot } };
}
