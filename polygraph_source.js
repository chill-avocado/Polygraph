/* ══════════════════════════════════════════════════════════════════════════
   polygraph_source.js — unified sample source for the app
   ───────────────────────────────────────────────────────────────────────────
   The app subscribes to one thing and doesn't care where samples come from:

     • LiveSource — WebSocket to the ESP32; parses batched "t,gsr,ir" frames.
     • DemoSource — drives the synthetic generator at a steady simulated 100 Hz
                    off wall-clock elapsed time (immune to timer jitter), so the
                    whole flow works with no hardware.

   Both call onSample(tMs, gsr, ir). connect()/disconnect() manage lifecycle.
   ══════════════════════════════════════════════════════════════════════════ */

'use strict';

import { SyntheticSource } from './polygraph_synth.js';

// Parse one WebSocket text frame (one-or-more "t,gsr,ir" lines) → array of
// [t, gsr, ir]. Exported so it can be unit-tested without a socket.
export function parseFrame(text) {
  const out = [];
  for (const line of text.split('\n')) {
    if (!line) continue;
    const p = line.split(',');
    if (p.length < 3 || p[0] === '' || p[1] === '' || p[2] === '') continue;
    const t = +p[0], gsr = +p[1], ir = +p[2];
    if (Number.isFinite(t) && Number.isFinite(gsr) && Number.isFinite(ir)) out.push([t, gsr, ir]);
  }
  return out;
}

export class LiveSource {
  constructor(url) { this.url = url; this.ws = null; this.onSample = null; this.onStatus = null; }
  connect(onSample, onStatus) {
    this.onSample = onSample; this.onStatus = onStatus || (() => {});
    this.ws = new WebSocket(this.url);
    this.ws.onopen = () => this.onStatus('connected');
    this.ws.onclose = () => this.onStatus('disconnected');
    this.ws.onerror = () => this.onStatus('error');
    this.ws.onmessage = ev => {
      const rows = parseFrame(typeof ev.data === 'string' ? ev.data : '');
      for (const [t, gsr, ir] of rows) this.onSample(t, gsr, ir);
    };
  }
  disconnect() { if (this.ws) { this.ws.onclose = null; this.ws.close(); this.ws = null; } }
}

export class DemoSource {
  constructor({ fs = 100, seed = Date.now() & 0xffff } = {}) {
    this.fs = fs;
    this.dtMs = 1000 / fs;
    this.synth = new SyntheticSource({ fs, seed });
    this.onSample = () => {};
    this.raf = null;
    this.simT = 0;          // monotonic simulated device time (ms)
    this.wallStart = 0;
    this.relaxTimer = null;
    this.realtime = false;
  }

  connect(onSample, onStatus) { this.onSample = onSample; if (onStatus) onStatus('demo'); }
  disconnect() { this.stopRealtime(); }

  _emit(t) { const s = this.synth.sample(t); this.onSample(t, s.gsr, s.ir); }

  // Synthesize `seconds` of data INSTANTLY (not paced to wall-clock). Used to
  // fill the 2-minute baseline without waiting two real minutes.
  burst(seconds, { arousal = null } = {}) {
    if (arousal != null) this.synth.setArousal(arousal);
    const n = Math.round(seconds * this.fs);
    for (let i = 0; i < n; i++) { this.simT += this.dtMs; this._emit(this.simT); }
    // Rebase wall-clock so a running real-time stream continues from here.
    this.wallStart = performance.now() - this.simT;
  }

  // Stream at a true 100 Hz off elapsed wall-clock time (immune to rAF jitter).
  startRealtime() {
    if (this.realtime) return;
    this.realtime = true;
    this.wallStart = performance.now() - this.simT;
    const tick = () => {
      if (!this.realtime) return;
      const elapsed = performance.now() - this.wallStart;
      while (this.simT + this.dtMs <= elapsed) { this.simT += this.dtMs; this._emit(this.simT); }
      this.raf = requestAnimationFrame(tick);
    };
    this.raf = requestAnimationFrame(tick);
  }
  stopRealtime() { this.realtime = false; if (this.raf) cancelAnimationFrame(this.raf); this.raf = null; }

  // Make the synthetic "subject" react, so the flow can demonstrate a Deceptive
  // read. Honest: the engine still only sees physiology and scores it.
  setArousal(a) { this.synth.setArousal(a); }
  reactNow(amp = 120, holdMs = 4500) {
    this.synth.triggerSCR(this.simT + 400, amp);
    this.synth.setArousal(0.9);
    clearTimeout(this.relaxTimer);
    this.relaxTimer = setTimeout(() => this.synth.setArousal(0.06), holdMs);
  }
}

// Factory: pick a source from a mode string ('demo' | ws URL).
export function makeSource(mode) {
  if (!mode || mode === 'demo') return new DemoSource();
  return new LiveSource(mode);
}
