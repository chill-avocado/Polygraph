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

// Parse one WebSocket text frame → array of [t, gsr, ir].
//
// Wire contract (the firmware emits exactly this):
//   • A frame is one OR MORE newline-separated CSV lines (batched).
//   • Each line is `t,gsr,ir[,red]`  (the 4th `red` column is optional).
//   • Lines may be CRLF-terminated (a trailing '\r' is stripped).
// We read only col[0]=t, col[1]=gsr, col[2]=ir and ignore any extra columns.
// Malformed/partial lines (too few columns, empty fields, non-finite numbers)
// are skipped — never thrown. Exported so it can be unit-tested without a socket.
export function parseFrame(text) {
  const out = [];
  if (typeof text !== 'string' || text.length === 0) return out;
  const lines = text.split('\n');
  for (let i = 0; i < lines.length; i++) {
    let line = lines[i];
    if (!line) continue;
    // Tolerate CRLF: drop a trailing carriage return so the last field is clean.
    if (line.charCodeAt(line.length - 1) === 13) line = line.slice(0, -1);
    if (!line) continue;
    const p = line.split(',');
    if (p.length < 3) continue;                                   // partial/short line
    if (p[0] === '' || p[1] === '' || p[2] === '') continue;      // empty field
    const t = +p[0], gsr = +p[1], ir = +p[2];                     // col[3]=red ignored
    if (Number.isFinite(t) && Number.isFinite(gsr) && Number.isFinite(ir)) out.push([t, gsr, ir]);
  }
  return out;
}

export class LiveSource {
  // Options let tests drive backoff quickly; defaults are production values.
  constructor(url, { baseBackoffMs = 250, maxBackoffMs = 5000, maxRetries = Infinity } = {}) {
    this.url = url;
    this.ws = null;
    this.onSample = () => {};
    this.onStatus = () => {};
    this.baseBackoff = baseBackoffMs;
    this.maxBackoff = maxBackoffMs;
    this.maxRetries = maxRetries;
    this.backoff = baseBackoffMs;
    this.attempts = 0;             // consecutive failed connects (reset on open)
    this.reconnectTimer = null;
    this.intentional = false;      // set by disconnect() → suppresses reconnect
    this.lastT = -Infinity;        // last DELIVERED device t; persists across reconnects
  }

  connect(onSample, onStatus) {
    this.onSample = onSample || (() => {});
    this.onStatus = onStatus || (() => {});
    this.intentional = false;
    this.backoff = this.baseBackoff;
    this.attempts = 0;
    this.onStatus('connecting');
    this._open();
  }

  _open() {
    let ws;
    try {
      ws = new WebSocket(this.url);
    } catch (e) {
      // Construction itself failed (bad URL / blocked) — treat as an error and retry.
      this.onStatus('error');
      this._scheduleReconnect();
      return;
    }
    this.ws = ws;
    // Prefer ArrayBuffer so any binary frame decodes synchronously (in order).
    try { ws.binaryType = 'arraybuffer'; } catch (e) { /* not settable in some stacks */ }

    ws.onopen = () => {
      this.backoff = this.baseBackoff;   // reset backoff on a good connection
      this.attempts = 0;
      this.onStatus('connected');
    };
    ws.onerror = () => { this.onStatus('error'); };
    ws.onmessage = (ev) => this._onMessage(ev);
    ws.onclose = () => {
      this.ws = null;
      if (this.intentional) return;      // disconnect() already emitted 'disconnected'
      this._scheduleReconnect();
    };
  }

  // Decode one frame (string | ArrayBuffer | typed array | Blob) → samples.
  // One malformed frame is skipped, never fatal to the message loop.
  _onMessage(ev) {
    try {
      const data = ev && ev.data;
      if (typeof data === 'string') { this._ingestText(data); return; }
      if (data instanceof ArrayBuffer) { this._ingestText(this._decode(data)); return; }
      if (ArrayBuffer.isView(data)) { this._ingestText(this._decode(data.buffer)); return; }
      if (typeof Blob !== 'undefined' && data instanceof Blob) {
        // Rare async path (only if binaryType stayed 'blob'); order preserved
        // within a single Blob's samples.
        data.text().then(txt => this._ingestText(txt)).catch(() => {});
        return;
      }
      // Unknown payload type → ignore silently.
    } catch (e) { /* never let one frame break streaming */ }
  }

  _decode(buf) {
    try { return new TextDecoder().decode(new Uint8Array(buf)); }
    catch (e) { return ''; }
  }

  // Parse + deliver samples in order. Timestamp hygiene: pass device t through
  // as-is, but DROP any sample whose t is not strictly greater than the last
  // delivered t (kills duplicate/rewound frames re-sent after a reconnect).
  _ingestText(text) {
    let rows;
    try { rows = parseFrame(text); } catch (e) { return; }
    for (let i = 0; i < rows.length; i++) {
      const t = rows[i][0], gsr = rows[i][1], ir = rows[i][2];
      if (!(t > this.lastT)) continue;   // also rejects NaN/dupes/rewinds
      this.lastT = t;
      try { this.onSample(t, gsr, ir); }
      catch (e) { /* a bad consumer must not stall the rest of the batch */ }
    }
  }

  _scheduleReconnect() {
    if (this.intentional) return;
    this.attempts += 1;
    if (this.attempts > this.maxRetries) {
      this.onStatus('disconnected');     // final give-up
      return;
    }
    this.onStatus('reconnecting');
    const delay = this.backoff;
    this.backoff = Math.min(this.backoff * 2, this.maxBackoff);  // exponential, capped
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      if (this.intentional) return;
      this._open();
    }, delay);
  }

  disconnect() {
    this.intentional = true;
    if (this.reconnectTimer) { clearTimeout(this.reconnectTimer); this.reconnectTimer = null; }
    const ws = this.ws;
    this.ws = null;
    if (ws) {
      ws.onopen = ws.onmessage = ws.onerror = null;
      ws.onclose = null;                 // don't let the close trigger a reconnect
      try { ws.close(); } catch (e) { /* already closed */ }
    }
    this.onStatus('disconnected');
  }
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
