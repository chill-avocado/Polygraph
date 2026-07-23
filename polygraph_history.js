/* ══════════════════════════════════════════════════════════════════════
   polygraph_history.js — session history + export module

   Self-contained, dependency-free ES module. Persists completed-exam
   sessions per profile in localStorage, exports them as JSON/CSV, and
   renders a styled history list into a container using the host app's
   palette CSS variables.

   Storage model
   ─────────────
   One list per profile, keyed `polygraph:sessions:<profileId>`, stored
   newest-first and capped to the most recent MAX_SESSIONS. All storage
   access is wrapped so a full/unavailable/corrupt store degrades to an
   empty list instead of throwing at the caller.

   Session record shape (flexible — future multi-question aggregates fit):
     { id, profileId, subjectName, ts,           // epoch ms
       determination,                             // 'Deceptive'|'Truthful'|'Inconclusive'
       reactivity, confidence, baselineReady,
       channels: { scrAmp:{value,z}, scl:{…}, hr:{…}, pulseAmp:{…} },
       questions?: [{ text, type, label, reactivity }],
       notes? }
   ══════════════════════════════════════════════════════════════════════ */

const KEY_PREFIX = 'polygraph:sessions:';
const MAX_SESSIONS = 100;

// Channel order used for CSV columns and (implicitly) any per-channel display.
const CHANNELS = ['scrAmp', 'scl', 'hr', 'pulseAmp'];

// Determination → palette variable + hard-coded fallback (for non-DOM/export
// contexts where the CSS var is meaningless). Mirrors the app's result screen.
const DETERMINATION_COLORS = {
  Deceptive:    { var: 'var(--z-hot)',      hex: '#c15b4e' },
  Truthful:     { var: 'var(--green-deep)', hex: '#6fae8b' },
  Inconclusive: { var: 'var(--amber)',      hex: '#ea9b4e' },
};

/* ── environment guards ─────────────────────────────────────────────── */

function getStore() {
  try {
    const ls = (typeof globalThis !== 'undefined' && globalThis.localStorage) || null;
    // Touch it so a SecurityError (private mode / disabled storage) is caught here.
    if (ls && typeof ls.getItem === 'function') return ls;
  } catch (_) { /* fall through */ }
  return null;
}

function keyFor(profileId) {
  return KEY_PREFIX + String(profileId == null ? '' : profileId);
}

/* ── low-level read/write (never throw) ─────────────────────────────── */

// Read + sanitise the list for a profile. Corrupt JSON → []; individual
// non-object entries are skipped rather than poisoning the whole list.
function readList(profileId) {
  const store = getStore();
  if (!store) return [];
  let raw;
  try { raw = store.getItem(keyFor(profileId)); }
  catch (_) { return []; }
  if (!raw) return [];
  let parsed;
  try { parsed = JSON.parse(raw); }
  catch (_) { return []; }              // corrupt — treat as empty
  if (!Array.isArray(parsed)) return [];
  const out = [];
  for (const item of parsed) {
    if (item && typeof item === 'object') out.push(item);
  }
  return out;
}

// Persist a list. Returns true on success, false if storage is unavailable
// or rejects (e.g. quota). Never throws.
function writeList(profileId, list) {
  const store = getStore();
  if (!store) return false;
  try {
    store.setItem(keyFor(profileId), JSON.stringify(list));
    return true;
  } catch (_) {
    return false;
  }
}

/* ── id + normalisation ─────────────────────────────────────────────── */

function makeId() {
  const rnd = Math.random().toString(36).slice(2, 8);
  return 's' + Date.now().toString(36) + rnd;
}

// Normalise a caller-supplied session (which may be a raw engine result plus
// context, or an already-shaped record) into the stored record shape. Tolerant
// of missing fields; accepts either `determination` or the engine's `label`.
function normalize(profileId, session) {
  const s = (session && typeof session === 'object') ? session : {};
  const rec = {
    id: s.id != null ? String(s.id) : makeId(),
    profileId: profileId != null ? String(profileId) : (s.profileId != null ? String(s.profileId) : ''),
    subjectName: typeof s.subjectName === 'string' ? s.subjectName : (typeof s.subject === 'string' ? s.subject : ''),
    ts: Number.isFinite(s.ts) ? s.ts : Date.now(),
    determination: s.determination || s.label || 'Inconclusive',
    reactivity: Number.isFinite(s.reactivity) ? s.reactivity : null,
    confidence: Number.isFinite(s.confidence) ? s.confidence : null,
    baselineReady: s.baselineReady === undefined ? null : !!s.baselineReady,
    channels: (s.channels && typeof s.channels === 'object') ? s.channels : {},
  };
  if (Array.isArray(s.questions)) rec.questions = s.questions;
  if (s.notes != null) rec.notes = s.notes;
  return rec;
}

/* ══════════════════════════════════════════════════════════════════════
   PUBLIC — storage CRUD
   ══════════════════════════════════════════════════════════════════════ */

// Append a completed session for a profile. Assigns id + ts if absent,
// stores newest-first, caps to MAX_SESSIONS. Returns the stored record
// (even if persistence failed, so the caller can still use it in-memory).
export function saveSession(profileId, session) {
  const rec = normalize(profileId, session);
  const list = readList(profileId);
  // De-dupe by id (idempotent re-save / replace).
  const filtered = list.filter(x => x && x.id !== rec.id);
  filtered.unshift(rec);                       // newest first
  if (filtered.length > MAX_SESSIONS) filtered.length = MAX_SESSIONS;
  writeList(profileId, filtered);
  return rec;
}

// All sessions for a profile, newest first.
export function listSessions(profileId) {
  return readList(profileId);
}

// One session by id, or null.
export function getSession(profileId, id) {
  const target = String(id);
  const found = readList(profileId).find(x => x && String(x.id) === target);
  return found || null;
}

// Remove one session by id. Returns true if something was removed.
export function deleteSession(profileId, id) {
  const target = String(id);
  const list = readList(profileId);
  const next = list.filter(x => x && String(x.id) !== target);
  if (next.length === list.length) return false;
  writeList(profileId, next);
  return true;
}

// Drop all sessions for a profile.
export function clearSessions(profileId) {
  const store = getStore();
  if (!store) return false;
  try { store.removeItem(keyFor(profileId)); return true; }
  catch (_) { return false; }
}

/* ══════════════════════════════════════════════════════════════════════
   PUBLIC — export helpers
   ══════════════════════════════════════════════════════════════════════ */

// Pretty-printed JSON for a single session.
export function exportSessionJSON(session) {
  try { return JSON.stringify(session, null, 2); }
  catch (_) { return '{}'; }
}

function csvCell(v) {
  if (v == null) return '';
  const s = String(v);
  // Quote if it contains a comma, quote, or newline; double interior quotes.
  return /[",\n\r]/.test(s) ? '"' + s.replace(/"/g, '""') + '"' : s;
}

function pct(x) {
  return Number.isFinite(x) ? Math.round(x * 100) + '%' : '';
}

function isoTs(ts) {
  if (!Number.isFinite(ts)) return '';
  try { return new Date(ts).toISOString(); }
  catch (_) { return ''; }
}

function channelZ(session, ch) {
  const c = session && session.channels && session.channels[ch];
  const z = c && typeof c === 'object' ? c.z : undefined;
  return Number.isFinite(z) ? z : '';
}

// CSV over a list of sessions — one row per session. Header + per-channel z.
export function exportSessionCSV(sessions) {
  const rows = Array.isArray(sessions) ? sessions : (sessions ? [sessions] : []);
  const header = ['timestamp', 'subject', 'determination', 'reactivity%', 'confidence%',
    ...CHANNELS.map(c => c + '_z')];
  const lines = [header.map(csvCell).join(',')];
  for (const s of rows) {
    if (!s || typeof s !== 'object') continue;
    const cells = [
      isoTs(s.ts),
      s.subjectName || '',
      s.determination || s.label || '',
      pct(s.reactivity),
      pct(s.confidence),
      ...CHANNELS.map(c => channelZ(s, c)),
    ];
    lines.push(cells.map(csvCell).join(','));
  }
  return lines.join('\r\n');
}

// Trigger a browser download of text via a Blob + temporary <a>. No-op-safe
// (returns false) when not in a browser / Blob or URL unavailable.
export function downloadText(filename, text, mime) {
  try {
    if (typeof document === 'undefined' || typeof Blob === 'undefined') return false;
    const URLc = (typeof URL !== 'undefined' && URL) ||
      (typeof globalThis !== 'undefined' && globalThis.webkitURL) || null;
    if (!URLc || !URLc.createObjectURL) return false;
    const blob = new Blob([text == null ? '' : String(text)],
      { type: mime || 'text/plain;charset=utf-8' });
    const url = URLc.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = filename || 'download.txt';
    a.style.display = 'none';
    (document.body || document.documentElement).appendChild(a);
    a.click();
    a.remove();
    // Revoke on the next tick so the download has committed.
    setTimeout(() => { try { URLc.revokeObjectURL(url); } catch (_) {} }, 0);
    return true;
  } catch (_) {
    return false;
  }
}

/* ══════════════════════════════════════════════════════════════════════
   PUBLIC — DOM rendering
   ══════════════════════════════════════════════════════════════════════ */

const STYLE_ID = 'polygraph-history-styles';

// One scoped stylesheet, injected once. Uses the app's palette variables so
// it inherits light/theme colours; falls back to sensible neutrals.
function ensureStyles() {
  if (typeof document === 'undefined') return;
  if (document.getElementById(STYLE_ID)) return;
  const css = `
.pgh-wrap { display:flex; flex-direction:column; gap:.55rem; font: inherit; color: var(--text, #6b5b52); }
.pgh-head { display:flex; align-items:center; justify-content:space-between; gap:.75rem; margin-bottom:.15rem; }
.pgh-title { font-size:.8rem; letter-spacing:.04em; text-transform:uppercase; color: var(--text-muted, #a89a8c); }
.pgh-csv { appearance:none; border:1px solid var(--border-soft, #f2e9dd); background: var(--surface, #fff);
  color: var(--accent-deep, #c9a99a); font:inherit; font-size:.8rem; padding:.32rem .7rem; border-radius:.5rem;
  cursor:pointer; transition:color .15s, border-color .15s; }
.pgh-csv:hover { color: var(--text, #6b5b52); border-color: var(--accent-deep, #c9a99a); }
.pgh-list { display:flex; flex-direction:column; gap:.4rem; list-style:none; margin:0; padding:0; }
.pgh-row { display:flex; align-items:center; gap:.7rem; padding:.6rem .75rem; border-radius:.6rem;
  background: var(--surface, #fff); border:1px solid var(--border-soft, #f2e9dd); cursor:pointer;
  transition:border-color .15s, transform .05s; }
.pgh-row:hover { border-color: var(--accent-deep, #c9a99a); }
.pgh-row:active { transform: translateY(1px); }
.pgh-date { font-size:.85rem; color: var(--text, #6b5b52); min-width:8.5rem; }
.pgh-badge { font-size:.72rem; font-weight:600; letter-spacing:.02em; padding:.18rem .55rem; border-radius:1rem;
  color:#fff; white-space:nowrap; }
.pgh-react { margin-left:auto; font-size:.82rem; color: var(--text-muted, #a89a8c); font-variant-numeric:tabular-nums; }
.pgh-del { appearance:none; border:none; background:transparent; color: var(--text-muted, #a89a8c);
  font:inherit; font-size:1rem; line-height:1; padding:.1rem .3rem; cursor:pointer; border-radius:.35rem; }
.pgh-del:hover { color: var(--z-hot, #c15b4e); }
.pgh-empty { padding:1.4rem .75rem; text-align:center; color: var(--text-muted, #a89a8c); font-size:.9rem;
  border:1px dashed var(--border-soft, #f2e9dd); border-radius:.6rem; background: var(--surface, #fff); }
`;
  const el = document.createElement('style');
  el.id = STYLE_ID;
  el.textContent = css;
  (document.head || document.documentElement).appendChild(el);
}

function fmtDate(ts) {
  if (!Number.isFinite(ts)) return '—';
  const d = new Date(ts);
  try {
    return d.toLocaleString(undefined,
      { year: 'numeric', month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit' });
  } catch (_) {
    return d.toISOString();
  }
}

// Render the session history for a profile into containerEl. Callbacks are all
// optional:
//   onOpen(session)          — a row was clicked
//   onExport(sessions,'csv') — the "Export CSV (all)" affordance; if omitted,
//                              the component downloads the CSV itself
//   onDelete(session)        — a row's delete control; if omitted, the component
//                              deletes from storage itself. Either way the list
//                              re-renders from storage afterwards.
// Never throws; a null/absent container is a safe no-op.
export function renderHistoryInto(containerEl, profileId, opts) {
  if (typeof document === 'undefined' || !containerEl) return;
  const o = opts || {};
  try {
    ensureStyles();
    const sessions = listSessions(profileId);

    // Clear.
    while (containerEl.firstChild) containerEl.removeChild(containerEl.firstChild);

    const wrap = document.createElement('div');
    wrap.className = 'pgh-wrap';

    const head = document.createElement('div');
    head.className = 'pgh-head';
    const title = document.createElement('span');
    title.className = 'pgh-title';
    title.textContent = 'Session history';
    head.appendChild(title);

    if (sessions.length) {
      const csvBtn = document.createElement('button');
      csvBtn.type = 'button';
      csvBtn.className = 'pgh-csv';
      csvBtn.textContent = 'Export CSV (all)';
      csvBtn.addEventListener('click', (e) => {
        e.stopPropagation();
        if (typeof o.onExport === 'function') {
          o.onExport(sessions, 'csv');
        } else {
          const name = 'polygraph-sessions-' + String(profileId == null ? '' : profileId) + '.csv';
          downloadText(name, exportSessionCSV(sessions), 'text/csv;charset=utf-8');
        }
      });
      head.appendChild(csvBtn);
    }
    wrap.appendChild(head);

    if (!sessions.length) {
      const empty = document.createElement('div');
      empty.className = 'pgh-empty';
      empty.textContent = 'No past sessions yet.';
      wrap.appendChild(empty);
      containerEl.appendChild(wrap);
      return;
    }

    const list = document.createElement('ul');
    list.className = 'pgh-list';

    for (const s of sessions) {
      const row = document.createElement('li');
      row.className = 'pgh-row';
      row.setAttribute('data-session-id', String(s.id));

      const date = document.createElement('span');
      date.className = 'pgh-date';
      date.textContent = fmtDate(s.ts);
      row.appendChild(date);

      const det = s.determination || 'Inconclusive';
      const badge = document.createElement('span');
      badge.className = 'pgh-badge';
      const color = DETERMINATION_COLORS[det] || DETERMINATION_COLORS.Inconclusive;
      badge.style.background = color.var;
      badge.textContent = det;
      row.appendChild(badge);

      const react = document.createElement('span');
      react.className = 'pgh-react';
      react.textContent = Number.isFinite(s.reactivity) ? (Math.round(s.reactivity * 100) + '%') : '—';
      row.appendChild(react);

      const del = document.createElement('button');
      del.type = 'button';
      del.className = 'pgh-del';
      del.setAttribute('aria-label', 'Delete session');
      del.textContent = '×';
      del.addEventListener('click', (e) => {
        e.stopPropagation();
        if (typeof o.onDelete === 'function') o.onDelete(s);
        else deleteSession(profileId, s.id);
        renderHistoryInto(containerEl, profileId, opts);   // reflect fresh storage
      });
      row.appendChild(del);

      row.addEventListener('click', () => {
        if (typeof o.onOpen === 'function') o.onOpen(s);
      });

      list.appendChild(row);
    }

    wrap.appendChild(list);
    containerEl.appendChild(wrap);
  } catch (_) {
    // Rendering must never throw to the caller.
  }
}

/* Optional convenience: everything under one namespace too. */
export default {
  saveSession, listSessions, getSession, deleteSession, clearSessions,
  exportSessionJSON, exportSessionCSV, downloadText, renderHistoryInto,
};
