/* ══════════════════════════════════════════════════════════════════════════
   polygraph_protocol.js — Comparison Question Test (CQT) exam protocol
   ───────────────────────────────────────────────────────────────────────────
   PURE LOGIC. No DOM, no timers, no engine calls. Deterministic.

   This module turns a *sequence* of single-response scorings (produced by
   polygraph_engine.js `scoreResponse(startMs,endMs)`) into a structured
   multi-question exam with a per-question record and an aggregate CQT
   determination.

   ── How a CQT decides ──────────────────────────────────────────────────────
   Each question is one of three types:
     • 'relevant'   — the issue actually under test (e.g. "Did you take it?")
     • 'comparison' — a probable-lie control the subject is expected to react to
                      (e.g. "Before age 25, did you ever lie to stay out of
                      trouble?"). Reactivity here is the yardstick.
     • 'neutral'    — irrelevant filler (e.g. "Is today Tuesday?"). Kept for
                      display, but IGNORED by the determination.

   The determination compares physiological REACTIVITY (the engine's 0..1
   reactivity index, higher = more sympathetic arousal) to relevant vs
   comparison questions:
     • markedly higher reactivity to RELEVANT than COMPARISON ⇒ "Deceptive"
     • higher / roughly equal reactivity to COMPARISON         ⇒ "Truthful"
     • too close, or not enough usable signal                  ⇒ "Inconclusive"

   HONEST FRAMING (inherited from the engine): this measures within-subject
   arousal, not lies. Labels are a bounded mapping of an arousal contrast,
   surfaced with an explicit confidence.
   ══════════════════════════════════════════════════════════════════════════ */

'use strict';

/* ─── Tunable thresholds (all overridable via the Protocol opts arg) ──────── */

export const DEFAULT_PROTOCOL_OPTS = {
  // Minimum per-response signal-quality index (0..1) for a question to count
  // toward the determination. Matches the engine's own minSQI floor so we never
  // trust a determination the engine itself would have called Inconclusive.
  sqiFloor: 0.45,

  // Decision bands on delta = mean(relevant reactivity) − mean(comparison
  // reactivity). delta > 0 means the subject reacted MORE to the relevant
  // (issue-under-test) questions than to the probable-lie controls.
  deceptiveDelta: 0.10,   // delta ≥ this ⇒ Deceptive (clear relevant-side spike)
  truthfulDelta: -0.02,   // delta ≤ this ⇒ Truthful  (control-side ≥ relevant)
  //  (truthfulDelta … deceptiveDelta) is the inconclusive band.

  // ── Confidence shaping (see aggregate()) ──
  // |delta| at which the "magnitude" confidence term saturates to 1.
  confidenceDeltaScale: 0.20,
  // usable-question count at which the "sample size" term saturates to 1
  // (e.g. 2 relevant + 2 comparison).
  confidenceSampleTarget: 4,
  // Weights blending the three confidence terms; kept ≈ summing to 1.
  confWeightDelta: 0.5,   // how decisive the relevant-vs-comparison gap is
  confWeightSamples: 0.2, // how many usable questions backed it
  confWeightSqi: 0.3,     // mean signal quality of those questions
};

// The only determination strings we ever emit / accept.
const LABELS = Object.freeze(['Deceptive', 'Truthful', 'Inconclusive']);
const QUESTION_TYPES = Object.freeze(['relevant', 'comparison', 'neutral']);

/* ─── Small pure helpers ─────────────────────────────────────────────────── */

// Finite-number coercion: anything non-numeric / NaN / ±Inf becomes NaN so the
// usability check below rejects it uniformly.
function num(x) { return (typeof x === 'number' && Number.isFinite(x)) ? x : NaN; }
function mean(a) { return a.length ? a.reduce((s, v) => s + v, 0) / a.length : NaN; }
function clamp(v, lo, hi) { return Math.min(Math.max(v, lo), hi); }
function round(v, p = 3) { return Number.isFinite(v) ? +v.toFixed(p) : v; }

// Normalize an incoming score result (from engine.scoreResponse) into a stored,
// robust per-question record. Tolerates missing objects and NaN fields — any
// gap simply makes the record unusable rather than throwing.
function normalizeScore(score) {
  const s = score || {};
  const label = LABELS.includes(s.label) ? s.label : 'Inconclusive';
  return {
    reactivity: num(s.reactivity),
    label,
    confidence: num(s.confidence),
    sqi: num(s.sqi),
    // channels are display-only here; pass through untouched (or null).
    channels: (s.channels && typeof s.channels === 'object') ? s.channels : null,
    baselineReady: s.baselineReady === true,
  };
}

/* ─── Protocol — the CQT state machine + scorer ──────────────────────────── */

export class Protocol {
  /**
   * @param {Array<{id?, text, type:'relevant'|'comparison'|'neutral'}>} questions
   * @param {object} [opts]  overrides for DEFAULT_PROTOCOL_OPTS
   */
  constructor(questions = [], opts = {}) {
    this.opts = { ...DEFAULT_PROTOCOL_OPTS, ...(opts || {}) };

    // Normalize the question list. Unknown/absent types default to 'neutral'
    // (safe: neutral never drives a determination). Ids default to q1, q2, …
    const list = Array.isArray(questions) ? questions : [];
    this.questions = list.map((q, i) => ({
      id: (q && q.id != null) ? q.id : `q${i + 1}`,
      text: (q && q.text != null) ? String(q.text) : '',
      type: (q && QUESTION_TYPES.includes(q.type)) ? q.type : 'neutral',
    }));

    // Parallel array of recorded responses; null until a question is answered.
    this._records = new Array(this.questions.length).fill(null);

    // Pointer to the active (not-yet-answered) question. Also == # answered so far.
    this._index = 0;
  }

  /* ── State machine ─────────────────────────────────────────────────────── */

  // The active question (a {id,text,type,index}) or null when finished/empty.
  current() {
    if (this._index < 0 || this._index >= this.questions.length) return null;
    return { ...this.questions[this._index], index: this._index };
  }

  // True once every question has been passed (answered or skipped).
  get isComplete() {
    return this._index >= this.questions.length;
  }

  // {index, total}: index = questions completed so far == pointer position.
  progress() {
    return { index: Math.min(this._index, this.questions.length), total: this.questions.length };
  }

  // Attach the engine's score result to the CURRENT question and advance.
  // No-op (returns null) if the exam is already complete. Returns the stored
  // per-question record: {question, type, reactivity, label, confidence, sqi,
  // channels, usable}.
  recordResponse(scoreResult) {
    if (this.isComplete) return null;
    const q = this.questions[this._index];
    const n = normalizeScore(scoreResult);
    const record = {
      question: { ...q },       // {id,text,type}
      type: q.type,
      reactivity: n.reactivity,
      label: n.label,
      confidence: n.confidence,
      sqi: n.sqi,
      channels: n.channels,
      baselineReady: n.baselineReady,
      // Usable for the determination? Must be a scored (relevant|comparison)
      // question with a ready baseline, a finite reactivity, and sqi above the
      // floor. Neutrals are intentionally never usable (display only).
      usable: this._isUsable(q.type, n),
    };
    this._records[this._index] = record;
    this._index++;
    return record;
  }

  // Move past the current question WITHOUT recording a response (a skip).
  // Leaves that slot's record as null. Clamped; safe to over-call.
  advance() {
    if (this._index < this.questions.length) this._index++;
    return this.current();
  }

  // Wipe all responses and rewind to the first question. Question list is kept.
  reset() {
    this._records = new Array(this.questions.length).fill(null);
    this._index = 0;
  }

  // Is a normalized record usable for the relevant-vs-comparison contrast?
  _isUsable(type, n) {
    return (type === 'relevant' || type === 'comparison')
      && n.baselineReady === true
      && Number.isFinite(n.reactivity)
      && Number.isFinite(n.sqi)
      && n.sqi >= this.opts.sqiFloor;
  }

  /* ── Aggregate CQT determination ───────────────────────────────────────── */

  /**
   * Fold everything RECORDED SO FAR into a single CQT result. Safe to call
   * before the exam is complete — it simply aggregates the answered questions.
   * @returns {{
   *   determination:'Deceptive'|'Truthful'|'Inconclusive',
   *   relevantMean:number, comparisonMean:number, delta:number,
   *   confidence:number,
   *   perQuestion:Array<{text,type,reactivity,label,usable}>,
   *   reason:string,
   *   usableQuestions:number, insufficient:boolean
   * }}
   */
  aggregate() {
    const recorded = this._records.filter(r => r !== null);

    // Display list: EVERY answered question, including neutrals and unusable
    // ones, in exam order.
    const perQuestion = recorded.map(r => ({
      text: r.question.text,
      type: r.type,
      reactivity: r.reactivity,
      label: r.label,
      usable: r.usable,
    }));

    // Split usable questions by role.
    const rel = recorded.filter(r => r.usable && r.type === 'relevant');
    const cmp = recorded.filter(r => r.usable && r.type === 'comparison');
    const usableQuestions = rel.length + cmp.length;

    const O = this.opts;

    // ── Gate: a CQT needs at least one usable relevant AND one usable
    // comparison question. Without both there is nothing to contrast. ──
    if (rel.length === 0 || cmp.length === 0) {
      return {
        determination: 'Inconclusive',
        relevantMean: rel.length ? round(mean(rel.map(r => r.reactivity))) : NaN,
        comparisonMean: cmp.length ? round(mean(cmp.map(r => r.reactivity))) : NaN,
        delta: NaN,
        confidence: 0,
        perQuestion,
        reason: `Insufficient usable signal: need ≥1 relevant and ≥1 comparison `
          + `question with a ready baseline and sqi ≥ ${O.sqiFloor} `
          + `(have ${rel.length} relevant, ${cmp.length} comparison usable).`,
        usableQuestions,
        insufficient: true,
      };
    }

    // ── Core contrast ──
    const relevantMean = mean(rel.map(r => r.reactivity));
    const comparisonMean = mean(cmp.map(r => r.reactivity));
    const delta = relevantMean - comparisonMean;   // >0 ⇒ reacted more to the issue

    // ── Banded decision ──
    let determination;
    if (delta >= O.deceptiveDelta) determination = 'Deceptive';
    else if (delta <= O.truthfulDelta) determination = 'Truthful';
    else determination = 'Inconclusive';

    // ── Confidence: blend three independent, each-clamped 0..1 terms ──
    //  1) magnitude — how far |delta| is from "no difference", saturating at
    //     confidenceDeltaScale.
    const deltaTerm = clamp(Math.abs(delta) / O.confidenceDeltaScale, 0, 1);
    //  2) sample size — more usable questions ⇒ steadier means.
    const sampleTerm = clamp(usableQuestions / O.confidenceSampleTarget, 0, 1);
    //  3) signal quality — mean sqi of the usable questions.
    const sqiTerm = clamp(mean([...rel, ...cmp].map(r => r.sqi)), 0, 1);
    const confidence = clamp(
      O.confWeightDelta * deltaTerm
      + O.confWeightSamples * sampleTerm
      + O.confWeightSqi * sqiTerm,
      0, 1,
    );

    // ── Human-readable reason ──
    const d = round(delta), rm = round(relevantMean), cm = round(comparisonMean);
    let reason;
    if (determination === 'Deceptive') {
      reason = `Reactivity to relevant questions (${rm}) exceeded comparison (${cm}) `
        + `by Δ=${d} ≥ ${O.deceptiveDelta}.`;
    } else if (determination === 'Truthful') {
      reason = `Reactivity to relevant (${rm}) did not exceed comparison (${cm}); `
        + `Δ=${d} ≤ ${O.truthfulDelta}.`;
    } else {
      reason = `Relevant-vs-comparison difference Δ=${d} fell in the inconclusive band `
        + `(${O.truthfulDelta} … ${O.deceptiveDelta}); no clear contrast.`;
    }

    return {
      determination,
      relevantMean: rm,
      comparisonMean: cm,
      delta: d,
      confidence: round(confidence),
      perQuestion,
      reason,
      usableQuestions,
      insufficient: false,
    };
  }
}

/* ─── A sensible default question set the UI can ship with ────────────────────
   Ordering follows standard CQT practice: an easy neutral to settle the
   subject, then alternating comparison / relevant probes, closing on a neutral.
   EDIT the relevant questions to the actual issue under test. ─────────────── */

export const SAMPLE_QUESTIONS = Object.freeze([
  { id: 'n1', text: 'Is today a weekday?',                                   type: 'neutral' },
  { id: 'c1', text: 'Before this year, did you ever lie to avoid trouble?',  type: 'comparison' },
  { id: 'r1', text: 'Did you take the missing item?',                        type: 'relevant' },
  { id: 'n2', text: 'Are we in a testing room right now?',                   type: 'neutral' },
  { id: 'c2', text: 'In your whole life, did you ever break a promise?',     type: 'comparison' },
  { id: 'r2', text: 'Do you know who took the missing item?',                type: 'relevant' },
]);

export default Protocol;
