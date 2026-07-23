/* ══════════════════════════════════════════════════════════════════════════
   polygraph_charts.js — live real-time waveform strip charts
   ───────────────────────────────────────────────────────────────────────────
   A self-contained, dependency-free ES module that renders THREE stacked
   scrolling strip charts into a single <canvas>, driven live off the engine's
   ring buffer (engine.buf) and engine.liveVitals().

     1) EDA / GSR  — raw gsr channel, lightly smoothed, autoscaled to its
                     recent range.
     2) Pulse (PPG) — the ir channel band-limited to the cardiac band (a cheap
                      moving-average detrend + light smoothing) so individual
                      heartbeats are visible, normalized to the panel.
     3) HR trend   — engine.liveVitals().hr sampled over time (bpm), with the
                     current value labelled.

   Newest data is on the RIGHT; the view shows roughly the last `windowSec`
   seconds (default 8 s). Rendering is DPI-aware (scaled by
   window.devicePixelRatio) and driven by requestAnimationFrame. It PAUSES
   cleanly when .stop() is called or when the canvas is detached from the DOM,
   and only ever processes the currently-visible slice each frame (cheap).

   Contract it depends on (see polygraph_engine.js):
     • engine.buf = { t:[ms], gsr:[raw], ir:[raw] }  — chronological, newest
       last, ~100 Hz, ~45 s deep.
     • engine.liveVitals() → { hr, scl, sqi } | null  — null while warming up.

   Usage:
     import { LiveCharts } from './polygraph_charts.js';   // (also default export)
     const charts = new LiveCharts(canvasEl, engine);
     charts.start();   // when the capture screen becomes active
     charts.stop();    // when leaving the screen
   ══════════════════════════════════════════════════════════════════════════ */

'use strict';

/* ─── Palette resolution ───────────────────────────────────────────────────
   Read the app's CSS custom properties off the canvas's computed style so the
   charts track the app theme, with sensible fallbacks when a variable is
   absent (e.g. the standalone demo harness). ────────────────────────────── */
const PALETTE_KEYS = {
  eda:  ['--accent-deep', '#c9a99a'],   // EDA/GSR trace
  pulse:['--heart',       '#d98b82'],   // Pulse (PPG) trace
  hr:   ['--green-deep',  '#6fae8b'],   // HR trend line
  grid: ['--border-soft', '#f2e9dd'],   // gridlines / baselines
  text: ['--text-muted',  '#a89a8c'],   // labels
};

function resolvePalette(el) {
  let cs = null;
  try { cs = getComputedStyle(el); } catch (e) { /* no DOM style (tests) */ }
  const out = {};
  for (const key in PALETTE_KEYS) {
    const [varName, fallback] = PALETTE_KEYS[key];
    let v = '';
    if (cs) { try { v = cs.getPropertyValue(varName).trim(); } catch (e) {} }
    out[key] = v || fallback;
  }
  return out;
}

/* ─── Small numeric helpers ───────────────────────────────────────────────── */
function clamp(v, lo, hi) { return v < lo ? lo : (v > hi ? hi : v); }
// Exponential ease of a stored value toward a target — smooths autoscale so
// the vertical range glides instead of snapping frame-to-frame.
function ease(cur, target, k) { return cur + (target - cur) * k; }

/* ══════════════════════════════════════════════════════════════════════════
   LiveCharts
   ══════════════════════════════════════════════════════════════════════════ */
export class LiveCharts {
  /**
   * @param {HTMLCanvasElement} canvasEl  target canvas (sized via CSS)
   * @param {object} engine               the PolygraphEngine instance
   * @param {object} [opts]
   *   windowSec   {number} visible span in seconds            (default 8)
   *   hrSampleMs  {number} how often to sample liveVitals()    (default 500)
   *   panelGap    {number} px gap between the three panels      (default 10)
   *   lineWidth   {number} trace stroke width in CSS px         (default 1.5)
   *   showGrid    {boolean} draw faint baselines/gridlines      (default true)
   */
  constructor(canvasEl, engine, opts = {}) {
    if (!canvasEl) throw new Error('LiveCharts: canvas element required');
    this.canvas = canvasEl;
    this.engine = engine;
    this.ctx = canvasEl.getContext('2d');

    this.windowSec  = opts.windowSec  != null ? opts.windowSec  : 8;
    this.hrSampleMs = opts.hrSampleMs != null ? opts.hrSampleMs : 500;
    this.panelGap   = opts.panelGap   != null ? opts.panelGap   : 10;
    this.lineWidth  = opts.lineWidth  != null ? opts.lineWidth  : 1.5;
    this.showGrid   = opts.showGrid   !== false;

    this.colors = resolvePalette(canvasEl);

    // Backing-store size cache so we only resize (and clear) the canvas when
    // its CSS box or the device pixel ratio actually changes.
    this._cssW = 0; this._cssH = 0; this._dpr = 0;

    // Eased vertical autoscale state, per panel.
    this._edaScale   = null;              // { min, max }
    this._pulseAmp   = 1;                 // eased peak |amplitude| for the pulse panel
    this._hrScale    = null;              // { min, max } bpm

    // HR trend history — { t:[deviceMs], hr:[bpm] }, trimmed to the view.
    this._hr = { t: [], hr: [] };
    this._lastHrSampleAt = 0;             // performance.now() throttle
    this._currentHr = null;               // most-recent finite hr, for the label

    this._running = false;
    this._raf = 0;
    this._frame = this._frame.bind(this);
  }

  /* ── lifecycle ────────────────────────────────────────────────────────── */

  start() {
    if (this._running) return;
    this._running = true;
    this._lastHrSampleAt = 0;             // sample HR promptly on resume
    this._raf = requestAnimationFrame(this._frame);
  }

  stop() {
    this._running = false;
    if (this._raf) { cancelAnimationFrame(this._raf); this._raf = 0; }
  }

  /* ── per-frame render loop ────────────────────────────────────────────── */

  _frame(now) {
    if (!this._running) return;

    // Canvas detached from the document → pause cleanly (no reschedule). A
    // later start() resumes it.
    if (!this.canvas.isConnected) { this._running = false; this._raf = 0; return; }

    try { this._resizeIfNeeded(); } catch (e) {}

    // Hidden / zero-size (e.g. an inactive screen with display:none): keep the
    // loop alive cheaply but skip drawing until it has a real box again.
    if (this._cssW > 0 && this._cssH > 0) {
      this._sampleHR(now || (typeof performance !== 'undefined' ? performance.now() : Date.now()));
      try { this._draw(); } catch (e) { /* never let one frame kill the loop */ }
    }

    this._raf = requestAnimationFrame(this._frame);
  }

  // Match the backing store to the CSS box × devicePixelRatio, so 1 canvas unit
  // == 1 CSS px after the transform below (crisp on retina / high-DPI).
  _resizeIfNeeded() {
    const dpr = window.devicePixelRatio || 1;
    const cssW = this.canvas.clientWidth  || 0;
    const cssH = this.canvas.clientHeight || 0;
    if (cssW === this._cssW && cssH === this._cssH && dpr === this._dpr) return;
    this._cssW = cssW; this._cssH = cssH; this._dpr = dpr;
    this.canvas.width  = Math.max(1, Math.round(cssW * dpr));
    this.canvas.height = Math.max(1, Math.round(cssH * dpr));
    // All drawing below is expressed in CSS pixels.
    this.ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  }

  /* ── HR trend sampling (throttled; liveVitals() is comparatively costly) ── */
  _sampleHR(now) {
    if (now - this._lastHrSampleAt < this.hrSampleMs) return;
    this._lastHrSampleAt = now;
    let v = null;
    try { v = this.engine.liveVitals && this.engine.liveVitals(); } catch (e) {}
    if (!v || !Number.isFinite(v.hr)) return;
    const tEnd = this._latestMs();
    this._hr.t.push(tEnd);
    this._hr.hr.push(v.hr);
    this._currentHr = v.hr;
    // Trim history older than twice the view so the arrays stay small.
    const cutoff = tEnd - this.windowSec * 2000;
    let drop = 0;
    while (drop < this._hr.t.length && this._hr.t[drop] < cutoff) drop++;
    if (drop > 0) { this._hr.t.splice(0, drop); this._hr.hr.splice(0, drop); }
  }

  _latestMs() {
    const t = this.engine && this.engine.buf && this.engine.buf.t;
    return (t && t.length) ? t[t.length - 1] : 0;
  }

  /* ── main draw ────────────────────────────────────────────────────────── */
  _draw() {
    const ctx = this.ctx, W = this._cssW, H = this._cssH;
    ctx.clearRect(0, 0, W, H);                 // transparent background

    const buf = this.engine && this.engine.buf;
    const n = buf && buf.t ? buf.t.length : 0;
    const tEnd = n ? buf.t[n - 1] : 0;
    const windowMs = this.windowSec * 1000;
    const tStart = tEnd - windowMs;

    // Three equal panels stacked vertically with gaps between them.
    const gap = this.panelGap;
    const panelH = (H - gap * 2) / 3;
    const panels = [0, 1, 2].map(i => ({ top: i * (panelH + gap), h: panelH }));

    // Time → x maps oldest-visible (tStart) to the left edge, newest (tEnd) to
    // the right edge, so the trace scrolls right-to-left.
    const xForT = (t) => (windowMs > 0 ? (t - tStart) / windowMs * W : W);

    // First buffer index that falls inside the visible window (scan back from
    // the end so cost is O(visible), not O(buffer)).
    let startIdx = n;
    while (startIdx > 0 && buf.t[startIdx - 1] >= tStart) startIdx--;
    const visCount = n - startIdx;

    // Effective sample rate over the visible slice — used to size the pulse
    // filter windows without assuming exactly 100 Hz.
    let fsEst = 100;
    if (visCount >= 2) {
      const span = (buf.t[n - 1] - buf.t[startIdx]) / 1000;
      if (span > 0) fsEst = (visCount - 1) / span;
    }
    const haveSecond = visCount >= fsEst * 0.9;   // ~≥1 s of data present

    /* ── Panel 1: EDA / GSR — lightly smoothed, autoscaled ── */
    this._drawEDA(panels[0], buf, startIdx, n, xForT, haveSecond);

    /* ── Panel 2: Pulse — band-limited ir, normalized ── */
    this._drawPulse(panels[1], buf, startIdx, n, xForT, fsEst, haveSecond);

    /* ── Panel 3: HR trend ── */
    this._drawHR(panels[2], xForT, tStart, tEnd);
  }

  /* Panel chrome: faint baseline + the channel label. Returns the inner y-band
     (below the label) available for the waveform. */
  _panelFrame(p, label, labelColor) {
    const ctx = this.ctx, W = this._cssW;
    if (this.showGrid) {
      ctx.save();
      ctx.strokeStyle = this.colors.grid;
      ctx.globalAlpha = 0.9;
      ctx.lineWidth = 1;
      const midY = Math.round(p.top + p.h / 2) + 0.5;   // crisp 1px line
      ctx.beginPath(); ctx.moveTo(0, midY); ctx.lineTo(W, midY); ctx.stroke();
      ctx.restore();
    }
    ctx.save();
    ctx.font = '600 11px system-ui, -apple-system, "Segoe UI", sans-serif';
    ctx.textBaseline = 'top';
    ctx.fillStyle = labelColor || this.colors.text;
    ctx.fillText(label, 2, p.top + 2);
    ctx.restore();
    const pad = 15;                          // reserve room for the label at top
    return { y0: p.top + pad, y1: p.top + p.h - 3 };
  }

  // Draw a faint flat baseline in a panel's inner band (warming-up state).
  _drawBaseline(band) {
    const ctx = this.ctx, W = this._cssW;
    ctx.save();
    ctx.strokeStyle = this.colors.grid;
    ctx.globalAlpha = 0.7;
    ctx.lineWidth = 1;
    const y = Math.round((band.y0 + band.y1) / 2) + 0.5;
    ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(W, y); ctx.stroke();
    ctx.restore();
  }

  /* ── EDA / GSR ─────────────────────────────────────────────────────────── */
  _drawEDA(p, buf, startIdx, n, xForT, haveSecond) {
    const band = this._panelFrame(p, 'EDA', this.colors.text);
    if (!haveSecond) { this._drawBaseline(band); return; }

    // Light moving-average smoothing (~80 ms) over the visible gsr slice.
    const vis = n - startIdx;
    const win = clamp(Math.round(vis / (this.windowSec * 12.5)) | 0, 1, 25); // ~fs*0.08
    const xs = new Float64Array(vis);
    const ys = new Float64Array(vis);
    let mn = Infinity, mx = -Infinity;
    for (let i = 0; i < vis; i++) {
      const idx = startIdx + i;
      // Trailing moving average (cheap, causal).
      let acc = 0, cnt = 0;
      for (let k = i - win + 1; k <= i; k++) {
        if (k >= 0) { acc += buf.gsr[startIdx + k]; cnt++; }
      }
      const v = acc / cnt;
      xs[i] = xForT(buf.t[idx]);
      ys[i] = v;
      if (v < mn) mn = v; if (v > mx) mx = v;
    }

    // Eased autoscale with a minimum span so a flat signal doesn't fill the
    // panel with noise.
    let span = mx - mn; if (!(span > 0)) span = 1;
    const padSpan = span * 0.15;
    const tMin = mn - padSpan, tMax = mx + padSpan;
    if (!this._edaScale) this._edaScale = { min: tMin, max: tMax };
    else {
      this._edaScale.min = ease(this._edaScale.min, tMin, 0.08);
      this._edaScale.max = ease(this._edaScale.max, tMax, 0.08);
    }
    this._strokeTrace(xs, ys, this._edaScale.min, this._edaScale.max, band, this.colors.eda, vis);
  }

  /* ── Pulse (PPG) ───────────────────────────────────────────────────────── */
  _drawPulse(p, buf, startIdx, n, xForT, fsEst, haveSecond) {
    const band = this._panelFrame(p, 'Pulse', this.colors.text);
    if (!haveSecond) { this._drawBaseline(band); return; }

    const vis = n - startIdx;
    // Detrend: subtract a ~0.55 s trailing moving average (a high-pass with a
    // ~0.8 Hz corner) to kill DC + respiration; then a short 5-tap smooth to
    // tame ADC noise. What's left is the cardiac band — visible heartbeats.
    const wHi = clamp(Math.round(fsEst * 0.55), 5, 400);
    const wLo = clamp(Math.round(fsEst * 0.05), 1, 9);

    const ir = buf.ir, t = buf.t;
    const hp = new Float64Array(vis);
    let runAcc = 0;                 // rolling sum for the high-pass MA
    for (let i = 0; i < vis; i++) {
      const idx = startIdx + i;
      runAcc += ir[idx];
      if (i >= wHi) runAcc -= ir[idx - wHi];
      const cnt = Math.min(i + 1, wHi);
      hp[i] = ir[idx] - runAcc / cnt;
    }
    // Short smoothing pass + collect x + peak amplitude.
    const xs = new Float64Array(vis);
    const ys = new Float64Array(vis);
    let peak = 1e-6;
    let sAcc = 0;
    for (let i = 0; i < vis; i++) {
      sAcc += hp[i];
      if (i >= wLo) sAcc -= hp[i - wLo];
      const cnt = Math.min(i + 1, wLo);
      const v = sAcc / cnt;
      xs[i] = xForT(t[startIdx + i]);
      ys[i] = v;
      const a = v < 0 ? -v : v;
      if (a > peak) peak = a;
    }

    // Eased amplitude normalization; signal is centred on 0 in the panel.
    this._pulseAmp = ease(this._pulseAmp, peak, 0.1);
    const amp = this._pulseAmp > 1e-6 ? this._pulseAmp : peak;
    // Map centred so 0 sits mid-band; ±amp reaches ~90% of the half-height.
    this._strokeTrace(xs, ys, -amp / 0.9, amp / 0.9, band, this.colors.pulse, vis);
  }

  /* ── HR trend ──────────────────────────────────────────────────────────── */
  _drawHR(p, xForT, tStart, tEnd) {
    const hrLabel = 'HR ' + (this._currentHr != null ? Math.round(this._currentHr) : '--') + ' bpm';
    const band = this._panelFrame(p, hrLabel, this.colors.hr);

    const T = this._hr.t, HRv = this._hr.hr;
    // Collect the points inside the visible window.
    const xs = [], ys = [];
    let mn = Infinity, mx = -Infinity;
    for (let i = 0; i < T.length; i++) {
      if (T[i] < tStart || T[i] > tEnd) continue;
      xs.push(xForT(T[i]));
      ys.push(HRv[i]);
      if (HRv[i] < mn) mn = HRv[i]; if (HRv[i] > mx) mx = HRv[i];
    }
    if (xs.length < 1) { this._drawBaseline(band); return; }

    // Autoscale with a floor span (≥20 bpm) so a steady HR sits mid-panel
    // instead of jittering full-scale.
    let span = mx - mn;
    if (span < 20) { const c = (mn + mx) / 2; mn = c - 10; mx = c + 10; }
    else { mn -= span * 0.15; mx += span * 0.15; }
    if (!this._hrScale) this._hrScale = { min: mn, max: mx };
    else {
      this._hrScale.min = ease(this._hrScale.min, mn, 0.06);
      this._hrScale.max = ease(this._hrScale.max, mx, 0.06);
    }

    const ax = xs.length === 1 ? [xs[0], xs[0]] : xs;   // draw a dot as a nub
    const ay = ys.length === 1 ? [ys[0], ys[0]] : ys;
    this._strokeTrace(ax, ay, this._hrScale.min, this._hrScale.max, band, this.colors.hr, ax.length);
  }

  /* ── shared trace stroke ───────────────────────────────────────────────── */
  // Maps values (vMin..vMax) into the panel band (y1 bottom, y0 top) and
  // strokes a polyline. Decimates so no more than ~2 points per horizontal
  // pixel are drawn — keeps it cheap when the buffer is dense.
  _strokeTrace(xs, ys, vMin, vMax, band, color, count) {
    const ctx = this.ctx;
    const range = (vMax - vMin) || 1;
    const y0 = band.y0, y1 = band.y1, hgt = y1 - y0;
    const yForV = (v) => y1 - clamp((v - vMin) / range, 0, 1) * hgt;

    const maxPts = Math.max(2, Math.round(this._cssW * 2));
    const stride = count > maxPts ? Math.ceil(count / maxPts) : 1;

    ctx.save();
    ctx.beginPath();
    let started = false;
    for (let i = 0; i < count; i += stride) {
      const x = xs[i], y = yForV(ys[i]);
      if (!started) { ctx.moveTo(x, y); started = true; }
      else ctx.lineTo(x, y);
    }
    // Always include the very last sample so the trace reaches the right edge.
    if (count > 0 && (count - 1) % stride !== 0) {
      ctx.lineTo(xs[count - 1], yForV(ys[count - 1]));
    }
    ctx.strokeStyle = color;
    ctx.lineWidth = this.lineWidth;
    ctx.lineJoin = 'round';
    ctx.lineCap = 'round';
    ctx.stroke();
    ctx.restore();
  }
}

export default LiveCharts;
