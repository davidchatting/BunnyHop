// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * rabbit-api.js - client for the BunnyHop WebSocket API.
 *
 * Standalone: no dependency on p5.js or the rest of the web app, so it can
 * be reused by other pages and projects (copy this one file).
 *
 *   const rabbit = new RabbitClient();          // ws://<host>/ws
 *   rabbit.on('rfid', e => console.log(e.tag));
 *   rabbit.on('button', e => rabbit.ears(0, 16));
 *   await rabbit.leds(['#f00', '#0f0', '#00f', '#ff0', '#fff']);
 *
 * Events: 'open', 'close', 'hello', 'state', 'connection', 'phase', 'button',
 * 'ears', 'rfid', 'recording', 'boot', 'resource', 'pairing', 'scan', 'log',
 * 'xmpp-message', and '*' for every message.
 */
(function (root) {
  'use strict';

  const EAR_MAX = 16;

  class RabbitClient {
    constructor(url, options = {}) {
      const loc = typeof location !== 'undefined' ? location : null;
      this.url = url || (loc ? `ws://${loc.host}/ws` : 'ws://192.168.4.1/ws');
      this.reconnectMs = options.reconnectMs || 2000;
      this.timeoutMs = options.timeoutMs || 5000;
      // The rabbit can only take a few commands a second; continuous
      // controls (sliders, mouse) are coalesced to at most one per interval.
      this.throttleMs = options.throttleMs || 250;
      this.state = null;
      this.wifi = null;
      this.connected = false; // websocket to the bridge
      this._handlers = {};
      this._pending = new Map();
      this._nextId = 1;
      this._throttled = {};
      this._closedByUser = false;
      this.connect();
    }

    // ---------------- connection ----------------
    connect() {
      this._closedByUser = false;
      const ws = new WebSocket(this.url);
      this.ws = ws;
      ws.onopen = () => {
        this.connected = true;
        this._emit('open', {});
        // give the bridge the time so it can serve NTP to the rabbit
        this.send({ cmd: 'time', epochMs: Date.now() }).catch(() => {});
      };
      ws.onclose = () => {
        this.connected = false;
        for (const [, p] of this._pending) p.reject(new Error('connection closed'));
        this._pending.clear();
        this._emit('close', {});
        if (!this._closedByUser) setTimeout(() => this.connect(), this.reconnectMs);
      };
      ws.onerror = () => {};
      ws.onmessage = (m) => {
        let msg;
        try { msg = JSON.parse(m.data); } catch (e) { return; }
        this._handle(msg);
      };
    }

    close() {
      this._closedByUser = true;
      if (this.ws) this.ws.close();
    }

    get rabbitConnected() {
      return !!(this.state && this.state.connected);
    }

    _handle(msg) {
      if (msg.type === 'ack') {
        const p = this._pending.get(msg.id);
        if (p) {
          this._pending.delete(msg.id);
          clearTimeout(p.timer);
          msg.ok ? p.resolve(msg) : p.reject(Object.assign(new Error(msg.error || 'failed'), { reply: msg }));
        }
      } else if (msg.type === 'hello') {
        this.state = msg.state;
        this.wifi = msg.wifi;
      } else if (msg.type === 'state') {
        this.state = msg.state;
      } else if (this.state) {
        // keep the cached state current between periodic pushes
        if (msg.type === 'ears') { this.state.ears.left = msg.left; this.state.ears.right = msg.right; }
        if (msg.type === 'rfid') this.state.lastTag = msg.tag;
        if (msg.type === 'connection') this.state.connected = msg.connected;
        if (msg.type === 'phase') this.state.phase = msg.phase;
      }
      this._emit(msg.type, msg);
      this._emit('*', msg);
    }

    // ---------------- events ----------------
    on(type, fn) {
      (this._handlers[type] = this._handlers[type] || []).push(fn);
      return () => this.off(type, fn);
    }

    off(type, fn) {
      const h = this._handlers[type];
      if (h) this._handlers[type] = h.filter((f) => f !== fn);
    }

    _emit(type, msg) {
      for (const fn of this._handlers[type] || []) {
        try { fn(msg); } catch (e) { console.error(e); }
      }
    }

    // ---------------- commands ----------------
    /** Send any API command object; resolves with the ack. */
    send(cmd) {
      return new Promise((resolve, reject) => {
        if (!this.ws || this.ws.readyState !== 1) return reject(new Error('not connected to bridge'));
        const id = this._nextId++;
        const timer = setTimeout(() => {
          this._pending.delete(id);
          reject(new Error('timeout'));
        }, this.timeoutMs);
        this._pending.set(id, { resolve, reject, timer });
        this.ws.send(JSON.stringify(Object.assign({}, cmd, { id })));
      });
    }

    /**
     * Like send(), but for continuous inputs: only the latest command per
     * key is sent, at most once per throttleMs. Returns nothing.
     */
    sendThrottled(key, cmd) {
      const t = this._throttled[key] || (this._throttled[key] = { last: 0, cmd: null, timer: null });
      t.cmd = cmd;
      if (t.timer) return;
      const wait = Math.max(0, t.last + this.throttleMs - Date.now());
      t.timer = setTimeout(() => {
        t.timer = null;
        t.last = Date.now();
        this.send(t.cmd).catch(() => {});
      }, wait);
    }

    /** Ear positions 0..16, rabbit's own left/right (null/undefined leaves an ear alone). */
    ears(left, right, { throttle = false } = {}) {
      const c = { cmd: 'ears', left: clampEar(left), right: clampEar(right) };
      return throttle ? this.sendThrottled('ears', c) : this.send(c);
    }

    /** One LED: 0 bottom, 1 left, 2 middle, 3 right (rabbit's own sides), 4 nose; -1 = all. */
    led(id, color, { throttle = false } = {}) {
      const c = { cmd: 'led', led: id, color: toHex(color) };
      return throttle ? this.sendThrottled('leds', c) : this.send(c);
    }

    /** All five LEDs: [bottom, left, middle, right, nose]. */
    leds(colors, { throttle = false } = {}) {
      const c = { cmd: 'leds', colors: colors.map(toHex) };
      return throttle ? this.sendThrottled('leds', c) : this.send(c);
    }

    /** Play an audio file (URL on the bridge, e.g. '/audio/hello.mp3'). */
    play(url, wait = true) { return this.send({ cmd: 'play', url, wait }); }

    /**
     * Choreography: steps run in order; delay is in frames of tempo*10 ms.
     *   {type:'led', led:0..4, color:'#rgb', delay}
     *   {type:'motor', ear:0|1 (rabbit's left|right), pos:0..16, dir:0|1, delay}
     */
    chor(steps, tempo = 10) { return this.send({ cmd: 'chor', tempo, steps }); }

    /** Raw Violet program lines, e.g. "MU http://...\nMW". */
    program(text) { return this.send({ cmd: 'program', text }); }

    /** Violet ambient services, e.g. [[1, 3]] = weather: rain. */
    ambient(services) { return this.send({ cmd: 'ambient', services }); }

    sleep() { return this.send({ cmd: 'sleep' }); }
    wake() { return this.send({ cmd: 'wake' }); }
    reboot() { return this.send({ cmd: 'reboot' }); }
    raw(hex) { return this.send({ cmd: 'raw', hex }); }
    getState() { return this.send({ cmd: 'state' }).then((r) => r.data); }
    getWifi() { return this.send({ cmd: 'wifi' }).then((r) => r.data); }
    getLog() { return this.send({ cmd: 'log' }).then((r) => r.data); }
    audioList() { return this.send({ cmd: 'audio' }).then((r) => r.data); }
    pair() { return this.send({ cmd: 'pair' }); }
  }

  function clampEar(v) {
    // null/undefined/negative = leave this ear where it is
    if (v === null || v === undefined || Number.isNaN(+v) || +v < 0) return -1;
    return Math.max(0, Math.min(EAR_MAX, Math.round(+v)));
  }

  // Accepts '#rgb', '#rrggbb', [r,g,b], {r,g,b} or a p5.Color
  function toHex(c) {
    if (typeof c === 'string') {
      let s = c.replace('#', '');
      if (s.length === 3) s = s.split('').map((x) => x + x).join('');
      return '#' + s.toLowerCase();
    }
    let r, g, b;
    if (Array.isArray(c)) [r, g, b] = c;
    else if (c && c.levels) [r, g, b] = c.levels; // p5.Color
    else if (c) ({ r, g, b } = c);
    const h = (v) => Math.max(0, Math.min(255, Math.round(v || 0))).toString(16).padStart(2, '0');
    return '#' + h(r) + h(g) + h(b);
  }

  RabbitClient.EAR_MAX = EAR_MAX;
  RabbitClient.LEDS = ['bottom', 'left', 'middle', 'right', 'nose'];
  RabbitClient.toHex = toHex;

  if (typeof module !== 'undefined' && module.exports) module.exports = RabbitClient;
  else root.RabbitClient = RabbitClient;
})(typeof window !== 'undefined' ? window : globalThis);
