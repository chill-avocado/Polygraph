/* ══════════════════════════════════════════════════════════════════════════
   polygraph_synth.js — synthetic GSR + IR-PPG source
   ───────────────────────────────────────────────────────────────────────────
   Deterministic, physiologically-shaped fake sensor stream. Two jobs:
     • the app's DEMO mode (no ESP32 needed) — a drop-in for the WebSocket feed;
     • the engine's test fixtures (known-arousal traces with ground truth).

   Arousal ∈ [0,1] drives the sympathetic picture: HR up, HRV down, pulse
   amplitude down (vasoconstriction), tonic GSR up, and phasic SCRs on demand.
   ══════════════════════════════════════════════════════════════════════════ */

'use strict';

// Deterministic RNG so tests and demos are reproducible.
function mulberry32(seed) {
  let a = seed >>> 0;
  return function () {
    a |= 0; a = (a + 0x6D2B79F5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

// One PPG pulse over phase p ∈ [0,1): sharp systolic peak + smaller dicrotic.
function ppgWave(p) {
  const sys = Math.exp(-Math.pow((p - 0.18) / 0.10, 2));
  const dic = 0.35 * Math.exp(-Math.pow((p - 0.48) / 0.12, 2));
  return sys + dic;
}

export class SyntheticSource {
  constructor({ fs = 100, seed = 12345 } = {}) {
    this.fs = fs;
    this.rand = mulberry32(seed);
    this.cardiacPhase = 0;
    this.respPhase = 0;
    this.scl = 1800;          // tonic GSR (ADC counts)
    this.scrs = [];           // active phasic transients {t0, amp}
    this.arousal = 0.05;
    this.tPrev = null;
    this.hrOffset = 0;        // slow HR wander so a real baseline has spread
    this.sclDrift = 0;        // slow tonic wander
  }

  // Gaussian-ish noise via central limit of 3 uniforms.
  _noise() { return (this.rand() + this.rand() + this.rand() - 1.5) * 0.8; }

  setArousal(a) { this.arousal = clamp01(a); }

  // Fire a skin-conductance response now (amp in ~ADC-count units).
  triggerSCR(tMs, amp = 90) { this.scrs.push({ t0: tMs, amp }); }

  // Produce { gsr, ir } for absolute device time tMs. Call at ~fs Hz.
  sample(tMs) {
    const dt = this.tPrev == null ? 1 / this.fs : Math.max(1e-4, (tMs - this.tPrev) / 1000);
    this.tPrev = tMs;
    const a = this.arousal;

    // Slow physiological wander (bounded random walks) so a resting baseline
    // has realistic between-window spread rather than a single frozen value.
    this.hrOffset = clamp(this.hrOffset + (this.rand() - 0.5) * 0.05 * dt * this.fs, -3, 3);
    this.sclDrift = clamp(this.sclDrift + (this.rand() - 0.5) * 0.6 * dt * this.fs, -25, 25);

    // ── Cardiac ──
    const hr = 66 + 26 * a + this.hrOffset + 1.5 * this._noise();  // bpm
    // HRV: beat-to-beat jitter shrinks as arousal rises.
    const jitter = (0.05 - 0.035 * a) * this._noise();
    this.cardiacPhase += (hr / 60) * dt * (1 + jitter);
    const ampFactor = 1 - 0.4 * a;                          // vasoconstriction
    const cardiac = ppgWave(this.cardiacPhase - Math.floor(this.cardiacPhase));

    // ── Respiration (RIIV amplitude modulation + baseline sway) ──
    this.respPhase += 0.25 * dt;                            // ~15 breaths/min
    const resp = Math.sin(2 * Math.PI * this.respPhase);

    const irAC = 1600 * ampFactor * cardiac * (1 + 0.12 * resp);
    const irDC = 62000 + 350 * resp;
    const ir = irDC + irAC + 25 * this._noise();

    // ── Electrodermal ──
    const tonicTarget = 1800 + this.sclDrift + 260 * a;
    this.scl += (tonicTarget - this.scl) * clamp01(dt / 8);
    let scr = 0;
    for (const e of this.scrs) {
      const ts = (tMs - e.t0) / 1000;
      if (ts >= 0) scr += e.amp * (Math.exp(-ts / 3.0) - Math.exp(-ts / 0.6));
    }
    const gsr = this.scl + scr + 2.5 * this._noise();

    return { gsr, ir };
  }
}

function clamp01(v) { return Math.min(Math.max(v, 0), 1); }
function clamp(v, lo, hi) { return Math.min(Math.max(v, lo), hi); }
