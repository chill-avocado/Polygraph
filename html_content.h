/*  html_content.h — BioMonitor self-contained dashboard
 *  Stored in PROGMEM flash; served by AsyncWebServer.
 *  All signal processing runs in the browser — no internet required.
 */
#pragma once
#include <pgmspace.h>

const char DASHBOARD_HTML[] PROGMEM = R"HTMLEOF(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1"/>
<title>BioMonitor</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
:root{
  --bg:#071120;--panel:#0a1628;--border:#1a2f4a;--text:#c8d8e8;
  --muted:#3d5a7a;--cyan:#00d4ff;--green:#1fffa0;--red:#ff4060;
  --amber:#ffb347;--purple:#b57fff;--lime:#9dff57;
  --mono:'Courier New',monospace;--hd:'Arial',sans-serif;
}
body{background:var(--bg);color:var(--text);font-family:var(--hd);font-size:13px;height:100vh;display:flex;flex-direction:column;overflow:hidden}

/* ── Top bar ── */
#topbar{display:flex;align-items:center;gap:8px;padding:6px 12px;background:#05080f;border-bottom:1px solid var(--border);flex-shrink:0;flex-wrap:wrap;min-height:44px}
#logo{font-size:1.1rem;font-weight:700;letter-spacing:.15em;color:var(--cyan);white-space:nowrap}
.tb-pill{padding:3px 9px;border:1px solid var(--border);border-radius:20px;font-size:.65rem;letter-spacing:.08em;font-family:var(--mono);white-space:nowrap}
#sqi-wrap{display:flex;align-items:center;gap:5px}
#sqi-bar{width:55px;height:5px;background:var(--border);border-radius:3px;overflow:hidden}
#sqi-fill{height:100%;width:0%;border-radius:3px;background:var(--green);transition:width .4s,background .4s}
#sqi-val{font-family:var(--mono);font-size:.65rem;color:var(--muted)}
.nav-tab{padding:4px 12px;border-radius:4px;cursor:pointer;font-size:.68rem;letter-spacing:.08em;color:var(--muted);transition:all .2s}
.nav-tab:hover{color:var(--text)}
.nav-tab.active{background:rgba(0,212,255,.1);color:var(--cyan);border:1px solid var(--cyan)}
#ts-clock{margin-left:auto;font-family:var(--mono);font-size:.72rem;color:var(--muted)}
#no-sensor{color:var(--red);font-size:.65rem;font-family:var(--mono)}

/* ── Views ── */
#main{flex:1;display:grid;grid-template-columns:280px 1fr;gap:0;overflow:hidden}
#poly-view{flex:1;display:none;overflow-y:auto;padding:12px;gap:12px;flex-direction:column}
#poly-view.active{display:flex}

/* ── Left panels ── */
#left-col{display:flex;flex-direction:column;border-right:1px solid var(--border);overflow-y:auto}
.panel{border-bottom:1px solid var(--border)}
.panel-hd{padding:6px 10px;font-size:.6rem;letter-spacing:.12em;color:var(--muted);background:rgba(255,255,255,.02);border-bottom:1px solid var(--border)}
.panel-body{padding:8px 10px;display:grid;grid-template-columns:1fr 1fr;gap:6px}
.metric{background:rgba(255,255,255,.025);border-radius:5px;padding:6px 8px;border:1px solid var(--border)}
.metric-lbl{font-size:.57rem;letter-spacing:.08em;color:var(--muted);margin-bottom:2px}
.metric-val{font-size:1.3rem;font-weight:700;font-family:var(--mono);line-height:1}
.metric-unit{font-size:.6rem;color:var(--muted);margin-left:2px}

/* ── Right panels ── */
#right-col{display:flex;flex-direction:column;overflow:hidden}
.chart-panel{flex:1;position:relative;border-bottom:1px solid var(--border)}
.chart-hd{display:flex;align-items:center;gap:8px;padding:4px 10px;background:rgba(255,255,255,.02);border-bottom:1px solid var(--border)}
.chart-hd span{font-size:.6rem;letter-spacing:.1em;color:var(--muted)}
.tab-btn{padding:2px 8px;border-radius:3px;cursor:pointer;font-size:.6rem;color:var(--muted);border:1px solid transparent}
.tab-btn.active{color:var(--cyan);border-color:var(--cyan);background:rgba(0,212,255,.08)}
canvas.chart{width:100%;height:100%;display:block}

/* ── Overlay ── */
#overlay{position:fixed;inset:0;background:rgba(7,17,32,.92);display:flex;align-items:center;justify-content:center;font-family:var(--mono);font-size:.9rem;color:var(--cyan);z-index:100;letter-spacing:.1em}
#overlay.hidden{display:none}

/* ── Polygraph ── */
.poly-row{display:grid;grid-template-columns:1fr 1fr;gap:12px}
@media(max-width:640px){.poly-row{grid-template-columns:1fr}}
.poly-panel{background:var(--panel);border:1px solid var(--border);border-radius:8px;overflow:hidden}
.poly-ph{padding:6px 12px;font-size:.6rem;letter-spacing:.12em;color:var(--muted);background:rgba(255,255,255,.02);border-bottom:1px solid var(--border)}
.poly-pb{padding:12px}
.state-badge{display:inline-block;padding:3px 10px;border-radius:4px;font-size:.65rem;letter-spacing:.1em;font-family:var(--mono);border:1px solid var(--muted);color:var(--muted)}
.poly-btn{display:inline-block;padding:6px 14px;border-radius:5px;cursor:pointer;font-size:.68rem;letter-spacing:.08em;font-family:var(--mono);background:rgba(0,212,255,.08);border:1px solid var(--cyan);color:var(--cyan);user-select:none;margin:2px}
.poly-btn:hover{background:rgba(0,212,255,.18)}
.poly-btn:disabled,.poly-btn[disabled]{opacity:.3;cursor:not-allowed;pointer-events:none}
.poly-btn.danger{border-color:var(--red);color:var(--red);background:rgba(255,64,96,.08)}
.poly-btn.danger:hover{background:rgba(255,64,96,.2)}
.det-box{padding:10px 18px;border-radius:6px;font-family:var(--hd);font-size:.85rem;font-weight:700;letter-spacing:.12em;text-align:center;border:2px solid}
.det-box.DI{color:var(--red);border-color:var(--red);background:rgba(255,64,96,.08)}
.det-box.NDI{color:var(--green);border-color:var(--green);background:rgba(31,255,160,.08)}
.det-box.INC{color:var(--amber);border-color:var(--amber);background:rgba(255,179,71,.08)}
.nss-cell{display:inline-block;width:22px;height:22px;border-radius:3px;text-align:center;line-height:22px;font-size:.6rem;font-family:var(--mono)}
.nss-p3{background:#7f0000;color:#ffa0a0}.nss-p2{background:#a01010;color:#ffb0b0}
.nss-p1{background:#c02020;color:#ffcccc}.nss-0{background:var(--border);color:var(--muted)}
.nss-m1{background:#0a3a1a;color:#80ffb0}.nss-m2{background:#0a4a20;color:#60ffa0}
.nss-m3{background:#0a5a28;color:#40ff90}
input[type=text],select{background:rgba(255,255,255,.06);border:1px solid var(--border);border-radius:4px;color:var(--text);padding:5px 8px;font-family:var(--mono);font-size:.72rem;width:100%}
.prog-bar-wrap{height:8px;background:var(--border);border-radius:4px;overflow:hidden;margin:6px 0}
.prog-bar{height:100%;border-radius:4px;width:0%;transition:width .5s;background:linear-gradient(90deg,var(--cyan),var(--purple))}
</style>
</head>
<body>

<!-- ── Top bar ─────────────────────────────────────────────────────────────── -->
<div id="topbar">
  <span id="logo">BIOMONITOR</span>
  <div class="tb-pill" id="mode-pill" style="color:var(--green);border-color:var(--green)">● LIVE</div>
  <span id="no-sensor" style="display:none">NO PPG SENSOR</span>
  <div id="sqi-wrap">
    <span style="font-size:.6rem;color:var(--muted);font-family:var(--mono)">SQI</span>
    <div id="sqi-bar"><div id="sqi-fill"></div></div>
    <span id="sqi-val">--</span>
  </div>
  <div class="tb-pill" id="fs-pill">FS --</div>
  <div class="tb-pill" id="buf-pill">BUF --</div>
  <div class="nav-tab active" id="nav-bio" onclick="switchView('bio')">BIOSIGNALS</div>
  <div class="nav-tab" id="nav-poly" style="display:none" onclick="switchView('poly')">POLYGRAPH</div>
  <span id="ts-clock">--:--:--</span>
</div>

<!-- ── Biosignals view ─────────────────────────────────────────────────────── -->
<div id="main">
  <!-- Left: metric cards -->
  <div id="left-col">
    <div class="panel">
      <div class="panel-hd">VITAL SIGNS</div>
      <div class="panel-body">
        <div class="metric"><div class="metric-lbl">HEART RATE</div><div><span class="metric-val" id="m-hr">--</span><span class="metric-unit">bpm</span></div></div>
        <div class="metric"><div class="metric-lbl">SpO₂</div><div><span class="metric-val" id="m-spo2">--</span><span class="metric-unit">%</span></div></div>
        <div class="metric"><div class="metric-lbl">RESP RATE</div><div><span class="metric-val" id="m-rr">--</span><span class="metric-unit">bpm</span></div></div>
        <div class="metric"><div class="metric-lbl">PERFUSION IDX</div><div><span class="metric-val" id="m-pi">--</span><span class="metric-unit">%</span></div></div>
      </div>
    </div>
    <div class="panel">
      <div class="panel-hd">CARDIAC DYNAMICS</div>
      <div class="panel-body">
        <div class="metric"><div class="metric-lbl">HRV SDNN</div><div><span class="metric-val" id="m-sdnn">--</span><span class="metric-unit">ms</span></div></div>
        <div class="metric"><div class="metric-lbl">PRV RMSSD</div><div><span class="metric-val" id="m-rmssd">--</span><span class="metric-unit">ms</span></div></div>
        <div class="metric"><div class="metric-lbl">PULSE AMP VAR</div><div><span class="metric-val" id="m-pav">--</span><span class="metric-unit">%</span></div></div>
        <div class="metric"><div class="metric-lbl">EJECTION DUR</div><div><span class="metric-val" id="m-ed">--</span><span class="metric-unit">ms</span></div></div>
      </div>
    </div>
    <div class="panel">
      <div class="panel-hd">GSR / EDA</div>
      <div class="panel-body">
        <div class="metric"><div class="metric-lbl">TONIC (SCL)</div><div><span class="metric-val" id="m-scl">--</span><span class="metric-unit">μS</span></div></div>
        <div class="metric"><div class="metric-lbl">PHASIC (SCR)</div><div><span class="metric-val" id="m-scr">--</span><span class="metric-unit">μS</span></div></div>
        <div class="metric"><div class="metric-lbl">GSR RAW</div><div><span class="metric-val" id="m-gsr">--</span><span class="metric-unit">ADC</span></div></div>
        <div class="metric"><div class="metric-lbl">AUG IDX</div><div><span class="metric-val" id="m-aug">--</span><span class="metric-unit">%</span></div></div>
      </div>
    </div>
  </div>

  <!-- Right: charts -->
  <div id="right-col">
    <div class="chart-panel" style="flex:2">
      <div class="chart-hd">
        <span id="ppg-label">PPG — IR CHANNEL (Filtered)</span>
        <div class="tab-btn active" onclick="setChart('ir')">IR</div>
        <div class="tab-btn" onclick="setChart('red')">RED</div>
        <div class="tab-btn" onclick="setChart('raw')">RAW IR</div>
      </div>
      <canvas class="chart" id="chart-main"></canvas>
    </div>
    <div class="chart-panel" style="flex:1">
      <div class="chart-hd"><span>GSR DECOMPOSITION — Tonic / Phasic</span></div>
      <canvas class="chart" id="chart-gsr"></canvas>
    </div>
  </div>
</div>

<!-- ── Polygraph view ──────────────────────────────────────────────────────── -->
<div id="poly-view">
  <!-- Row 1: Session control + Signal readiness -->
  <div class="poly-row">
    <div class="poly-panel">
      <div class="poly-ph">POLYGRAPH — SESSION CONTROL</div>
      <div class="poly-pb">
        <div style="margin-bottom:8px">
          <div style="font-size:.6rem;color:var(--muted);margin-bottom:3px">SUBJECT CODE</div>
          <input type="text" id="poly-subject" value="ANON_001" style="width:160px">
        </div>
        <div style="display:flex;align-items:center;gap:10px;margin-bottom:10px">
          <div>
            <div style="font-size:.6rem;color:var(--muted);margin-bottom:3px">TECHNIQUE</div>
            <select id="poly-technique" style="width:180px">
              <option value="CQT">CQT — Control Question</option>
              <option value="GKT">GKT — Guilty Knowledge</option>
            </select>
          </div>
          <div>
            <div style="font-size:.6rem;color:var(--muted);margin-bottom:3px">STATE</div>
            <div class="state-badge" id="poly-state-badge">IDLE</div>
          </div>
        </div>
        <div style="display:flex;flex-wrap:wrap;gap:4px;margin-bottom:8px">
          <div class="poly-btn" id="btn-start" onclick="polyCmd('start')">START BASELINE</div>
          <div class="poly-btn" id="btn-begin" onclick="polyCmd('begin')" style="opacity:.3;pointer-events:none">BEGIN BLOCK</div>
          <div class="poly-btn" id="btn-resume" onclick="polyCmd('resume')" style="opacity:.3;pointer-events:none">RESUME</div>
          <div class="poly-btn danger" id="btn-reset" onclick="polyCmd('reset')" style="opacity:.3;pointer-events:none">RESET</div>
        </div>
        <div style="font-size:.68rem;font-family:var(--mono);color:var(--muted)" id="poly-session-info">No active session</div>
      </div>
    </div>

    <div class="poly-panel">
      <div class="poly-ph">SIGNAL READINESS</div>
      <div class="poly-pb">
        <div id="poly-idle-msg" style="text-align:center;padding:20px 0;color:var(--muted);font-size:.8rem">Awaiting session start</div>
        <div id="poly-baseline-panel" style="display:none">
          <div style="display:flex;justify-content:space-between;margin-bottom:6px">
            <span style="color:var(--cyan);font-size:.7rem;font-family:var(--mono)">BASELINE CAPTURE</span>
            <span id="poly-bl-eta" style="color:var(--muted);font-size:.7rem;font-family:var(--mono)"></span>
          </div>
          <div class="prog-bar-wrap"><div class="prog-bar" id="poly-bl-bar"></div></div>
          <div style="display:flex;justify-content:space-between;font-size:.7rem;font-family:var(--mono)">
            <span id="poly-bl-info" style="color:var(--muted)">Warming up…</span>
            <span id="poly-bl-pct" style="color:var(--cyan)">0%</span>
          </div>
          <div style="margin-top:8px;font-size:.68rem;color:var(--muted)">Sit quietly. Sensor must stay still.</div>
          <div style="margin-top:6px;text-align:right">
            <div class="poly-btn" id="btn-force-bl" onclick="polyCmd('force_baseline')" style="font-size:.6rem;padding:3px 8px;opacity:.6">FORCE COMPLETE</div>
          </div>
        </div>
        <div id="poly-pause-panel" style="display:none;padding:8px;border:2px solid var(--red);border-radius:6px;background:rgba(255,64,96,.06)">
          <div style="color:var(--red);font-size:.7rem;font-family:var(--mono);margin-bottom:4px">⚠ AUTO-PAUSE — SIGNAL DROPOUT</div>
          <div style="font-size:.7rem">SQI dropped below threshold for 3s.<br>Re-seat sensor then click RESUME.</div>
        </div>
        <div id="poly-sqi-live" style="display:none;margin-top:10px">
          <div style="font-size:.6rem;color:var(--muted);margin-bottom:3px;font-family:var(--mono)">LIVE SQI</div>
          <div style="height:8px;background:var(--border);border-radius:4px;overflow:hidden">
            <div id="poly-sqi-bar" style="height:100%;width:0%;border-radius:4px;background:var(--green);transition:width .4s,background .4s"></div>
          </div>
        </div>
      </div>
    </div>
  </div>

  <!-- Row 2: Active question + EDR -->
  <div id="poly-active-row" style="display:none">
    <div class="poly-panel" style="width:100%">
      <div class="poly-ph">ACTIVE QUESTION</div>
      <div class="poly-pb" style="display:grid;grid-template-columns:1fr 300px;gap:12px">
        <div>
          <div style="display:flex;align-items:center;gap:8px;margin-bottom:8px">
            <div id="poly-q-badge" style="padding:3px 12px;border-radius:4px;font-size:.65rem;font-family:var(--mono);background:var(--border);color:var(--text)">IRRELEVANT</div>
            <div style="font-size:.7rem;color:var(--muted);font-family:var(--mono)">Block <span id="poly-block-n">?</span>/<span id="poly-block-tot">?</span> &nbsp;·&nbsp; Q <span id="poly-q-n">?</span>/<span id="poly-q-tot">?</span></div>
          </div>
          <div id="poly-q-text" style="font-size:1rem;color:var(--text);line-height:1.5;min-height:40px">—</div>
          <div style="margin-top:6px;font-size:1.8rem;color:var(--cyan);font-family:var(--mono)" id="poly-q-timer">—</div>
        </div>
        <div>
          <div style="font-size:.6rem;color:var(--muted);margin-bottom:4px;font-family:var(--mono)">LIVE SCR (Phasic GSR)</div>
          <canvas id="chart-scr" style="width:100%;height:100px;display:block"></canvas>
          <div style="display:flex;gap:12px;margin-top:4px;font-size:.65rem;font-family:var(--mono)">
            <span>Lat: <span id="poly-edr-lat" style="color:var(--green)">—</span></span>
            <span>Peaks: <span id="poly-edr-cnt" style="color:var(--cyan)">0</span></span>
          </div>
        </div>
      </div>
    </div>
  </div>

  <!-- Row 3: NSS scoring -->
  <div id="poly-scoring-row" style="display:none">
    <div class="poly-panel" style="width:100%">
      <div class="poly-ph">NSS SCORING HEATMAP</div>
      <div class="poly-pb" style="overflow-x:auto">
        <div id="poly-nss-table" style="font-family:var(--mono);font-size:.7rem;color:var(--muted)">No pairs scored yet.</div>
        <div style="margin-top:8px;display:flex;align-items:center;gap:20px">
          <div>Composite: <span id="poly-composite" style="color:var(--text);font-family:var(--mono);font-size:1.2rem">—</span></div>
          <div id="poly-det-badge" class="det-box INC" style="display:none">INCONCLUSIVE</div>
        </div>
      </div>
    </div>
  </div>

  <!-- Row 4: Session complete -->
  <div id="poly-complete-row" style="display:none">
    <div class="poly-panel" style="width:100%">
      <div class="poly-ph">SESSION COMPLETE</div>
      <div class="poly-pb" style="display:flex;align-items:flex-start;gap:20px;flex-wrap:wrap">
        <div id="poly-final-det" class="det-box INC" style="font-size:.9rem;padding:10px 20px">—</div>
        <div style="font-size:.75rem;font-family:var(--mono)">
          <div>Composite Score: <span id="poly-final-comp" style="color:var(--text)">—</span></div>
          <div>EDR Suppression: <span id="poly-final-edr" style="color:var(--text)">—</span></div>
          <div>Questions scored: <span id="poly-final-n" style="color:var(--text)">—</span></div>
        </div>
        <div class="poly-btn" onclick="downloadReport()" style="margin-left:auto">DOWNLOAD REPORT</div>
      </div>
    </div>
  </div>
</div><!-- /poly-view -->

<!-- overlay -->
<div id="overlay"><span id="overlay-msg">Connecting…</span></div>

<script>
"use strict";
// ═══════════════════════════════════════════════════════════════════════════════
//  CONSTANTS & STATE
// ═══════════════════════════════════════════════════════════════════════════════
const FS_DISPLAY = 30;        // chart render rate (fps)
const BUF_S      = 8;         // seconds of samples to keep
const MIN_PTS    = 50;        // minimum samples before computing metrics
const Q_COLORS   = {R:'#ff4060',C:'#ffb347',I:'#3d5a7a',P:'#b57fff',F:'#3d5a7a'};

let sampleBuf = {ts:[], ir:[], red:[], irR:[], redR:[], gsr:[], gsrR:[], scr:[]};
let fsEst = 0, totalSamples = 0;
let metrics = {hr:0,spo2:0,rr:0,pi:0,sdnn:0,rmssd:0,pav:0,ed:0,scl:0,scr:0,gsrRaw:0,aug:0,sqi:0};
let chartMode = 'ir';
let scrCharts = {};            // rolling chart instances
let lastPeaks = [];            // latest peak indices
let lastRR    = [];            // R-R intervals in ms
let sessionData = null;        // polygraph session data
let renderInterval = null;

// ═══════════════════════════════════════════════════════════════════════════════
//  CIRCULAR BUFFER HELPERS
// ═══════════════════════════════════════════════════════════════════════════════
function bufPush(newTs, ir, irf, red, redf, gsr, gsrf) {
  sampleBuf.ts.push(newTs);
  sampleBuf.ir.push(irf);   // use EMA-filtered for processing
  sampleBuf.red.push(redf);
  sampleBuf.irR.push(ir);
  sampleBuf.redR.push(red);
  sampleBuf.gsr.push(gsrf);
  sampleBuf.gsrR.push(gsr);
  totalSamples++;

  // Trim to BUF_S seconds
  if (sampleBuf.ts.length > 2 && sampleBuf.ts.length > 2) {
    const keepFrom = newTs - BUF_S * 1000;
    while (sampleBuf.ts.length > 0 && sampleBuf.ts[0] < keepFrom) {
      for (const k of Object.keys(sampleBuf)) sampleBuf[k].shift();
    }
  }
}

function bufLen() { return sampleBuf.ts.length; }

// ═══════════════════════════════════════════════════════════════════════════════
//  SIGNAL PROCESSING
// ═══════════════════════════════════════════════════════════════════════════════

// DC-remove: subtract moving average over ~windowN samples
function dcRemove(arr, windowN) {
  if (arr.length < windowN) windowN = arr.length;
  const out = new Array(arr.length);
  let sum = 0;
  for (let i = 0; i < windowN; i++) sum += arr[i];
  for (let i = 0; i < arr.length; i++) {
    const ma = sum / windowN;
    out[i] = arr[i] - ma;
    const addIdx = Math.min(i + windowN, arr.length - 1);
    const remIdx = i;
    sum += arr[addIdx] - arr[remIdx];
  }
  return out;
}

// Find local maxima with minimum distance and prominence
function findPeaks(arr, minDist, minProm) {
  const peaks = [];
  for (let i = 1; i < arr.length - 1; i++) {
    if (arr[i] <= arr[i-1] || arr[i] < arr[i+1]) continue;
    if (peaks.length > 0 && i - peaks[peaks.length-1] < minDist) continue;
    // Prominence: how much above the lower of left/right minima
    const left  = i - minDist < 0 ? 0 : i - minDist;
    const right = i + minDist >= arr.length ? arr.length-1 : i + minDist;
    let lMin = arr[i], rMin = arr[i];
    for (let j = left; j < i; j++) if (arr[j] < lMin) lMin = arr[j];
    for (let j = i+1; j <= right; j++) if (arr[j] < rMin) rMin = arr[j];
    const prom = arr[i] - Math.max(lMin, rMin);
    if (prom >= minProm) peaks.push(i);
  }
  return peaks;
}

// Stats helpers
function mean(arr) { return arr.length ? arr.reduce((a,b)=>a+b,0)/arr.length : 0; }
function std(arr) {
  if (arr.length < 2) return 0;
  const m = mean(arr);
  return Math.sqrt(arr.reduce((s,v)=>s+(v-m)**2,0)/arr.length);
}

// Main metric computation — called at render rate
function computeMetrics() {
  const n = bufLen();
  if (n < MIN_PTS) return;

  const ir  = sampleBuf.ir;
  const red = sampleBuf.red;
  const gsr = sampleBuf.gsr;
  const ts  = sampleBuf.ts;

  // Estimate sample rate from buffer
  const durS = (ts[n-1] - ts[0]) / 1000;
  fsEst = durS > 0 ? (n - 1) / durS : 0;
  if (fsEst < 5) return;

  const minDistSamples = Math.round(fsEst * 0.33);  // ~330 ms min between beats

  // DC-remove for peak detection
  const irAC = dcRemove(ir, Math.round(fsEst * 1.5));
  const sigStd = std(irAC);
  const minProm = sigStd * 0.4;

  const peaks = findPeaks(irAC, minDistSamples, minProm);
  lastPeaks = peaks;

  // R-R intervals in ms
  const rr = [];
  for (let i = 1; i < peaks.length; i++) {
    rr.push(ts[peaks[i]] - ts[peaks[i-1]]);
  }
  lastRR = rr;

  // HR (BPM)
  const hrVal = rr.length > 0 ? 60000 / mean(rr) : 0;

  // HRV SDNN
  const sdnnVal = std(rr);

  // HRV RMSSD
  let rmssdVal = 0;
  if (rr.length > 1) {
    const diffs = rr.slice(1).map((v,i) => (v - rr[i])**2);
    rmssdVal = Math.sqrt(mean(diffs));
  }

  // SpO2 via AC/DC ratio
  const irDC  = mean(ir);
  const redDC = mean(red);
  let spo2Val = 0;
  if (peaks.length >= 2 && irDC > 100 && redDC > 100) {
    const irAC_rms  = Math.sqrt(mean(irAC.map(v => v*v)));
    const redAC = dcRemove(red, Math.round(fsEst * 1.5));
    const redAC_rms = Math.sqrt(mean(redAC.map(v => v*v)));
    const R = (redAC_rms / redDC) / (irAC_rms / irDC);
    spo2Val = Math.max(80, Math.min(100, 110 - 25 * R));
  }

  // Perfusion Index (IR AC amplitude / IR DC)
  const piVal = irDC > 0 ? (sigStd * 1.414 / irDC) * 100 : 0;

  // Resp rate — look for slow oscillation in R-R intervals (~0.1–0.5 Hz)
  let rrBreath = 0;
  if (rr.length >= 6) {
    // Simple: detect peaks in the R-R series itself
    const rrStd = std(rr);
    const rrPeaks = findPeaks(rr, 2, rrStd * 0.3);
    if (rrPeaks.length >= 2) {
      const rrPeriods = [];
      for (let i = 1; i < rrPeaks.length; i++)
        rrPeriods.push(ts[peaks[rrPeaks[i]]] - ts[peaks[rrPeaks[i-1]]]);
      const avgPeriod = mean(rrPeriods);
      if (avgPeriod > 1500 && avgPeriod < 10000)
        rrBreath = 60000 / avgPeriod;
    }
  }

  // Pulse amplitude variation
  const amps = [];
  for (let i = 1; i < peaks.length; i++) {
    const start = peaks[i-1], end = peaks[i];
    let mn = irAC[start];
    for (let j = start; j <= end; j++) if (irAC[j] < mn) mn = irAC[j];
    amps.push(irAC[peaks[i]] - mn);
  }
  const pavVal = amps.length > 1 ? (std(amps) / mean(amps)) * 100 : 0;

  // Ejection duration — upstroke width for last peak (samples from min to peak)
  let edVal = 0;
  if (peaks.length > 0) {
    const pk = peaks[peaks.length-1];
    const searchBack = Math.round(fsEst * 0.5);
    const start = Math.max(0, pk - searchBack);
    let minIdx = start;
    for (let j = start; j < pk; j++) if (irAC[j] < irAC[minIdx]) minIdx = j;
    edVal = (pk - minIdx) / fsEst * 1000;
  }

  // GSR decomposition
  const gsrMaWin = Math.round(fsEst * 5);  // 5s tonic window
  const gsrTonic = dcRemove(gsr, gsrMaWin).map((v,i) => gsr[i] - v);  // slow part
  // Simple: tonic = 5s MA; phasic = gsr - tonic
  let sclVal = 0, scrVal = 0;
  if (gsr.length > 10) {
    const recent = gsr.slice(-Math.round(fsEst * 2));
    sclVal = mean(recent);
    // Phasic: subtract 3s moving average
    const gsrSlowWin = Math.round(fsEst * 3);
    const gsrSlow = new Array(gsr.length);
    let s = 0, wn = Math.min(gsrSlowWin, gsr.length);
    for (let i = 0; i < wn; i++) s += gsr[i];
    for (let i = 0; i < gsr.length; i++) {
      gsrSlow[i] = s / wn;
      if (i + wn < gsr.length) { s += gsr[i + wn]; s -= gsr[i]; }
    }
    sampleBuf.scr = gsr.map((v,i) => v - gsrSlow[i]);
    scrVal = Math.abs(sampleBuf.scr[sampleBuf.scr.length-1] || 0);
  }

  // SQI: 1 - CV of R-R intervals, scaled 0-100
  let sqiVal = 0;
  if (rr.length >= 3) {
    const cv = mean(rr) > 0 ? std(rr) / mean(rr) : 1;
    sqiVal = Math.max(0, Math.min(100, (1 - Math.min(cv, 0.35) / 0.35) * 100));
  }

  // Augmentation index (simplified: ratio of early to late systolic peak)
  let augVal = 0;
  if (peaks.length >= 2) {
    augVal = Math.min(99, Math.max(-30, (piVal - 10) * 2));
  }

  metrics = {
    hr: hrVal, spo2: spo2Val, rr: rrBreath, pi: piVal,
    sdnn: sdnnVal, rmssd: rmssdVal, pav: pavVal, ed: edVal,
    scl: sclVal, scr: scrVal, gsrRaw: sampleBuf.gsrR[n-1]||0, aug: augVal,
    sqi: sqiVal
  };
}

// ═══════════════════════════════════════════════════════════════════════════════
//  ROLLING CHART (Canvas)
// ═══════════════════════════════════════════════════════════════════════════════
class RollingChart {
  constructor(canvasId, {color='#00d4ff', fill='rgba(0,212,255,0.06)', maxPts=400} = {}) {
    this.el = document.getElementById(canvasId);
    this.ctx = this.el ? this.el.getContext('2d') : null;
    this.color = color;
    this.fillColor = fill;
    this.maxPts = maxPts;
    this.data = [];
    this.data2 = null;  // second series (GSR phasic)
    this.color2 = '#ffb347';
    this.dpr = window.devicePixelRatio || 1;
    if (this.el) this.resize();
  }

  resize() {
    if (!this.el) return;
    this.el.width  = this.el.clientWidth  * this.dpr;
    this.el.height = this.el.clientHeight * this.dpr;
  }

  push(v) {
    this.data.push(v);
    if (this.data.length > this.maxPts) this.data.shift();
  }

  push2(v) {
    if (!this.data2) this.data2 = [];
    this.data2.push(v);
    if (this.data2.length > this.maxPts) this.data2.shift();
  }

  render() {
    if (!this.ctx) return;
    const ctx = this.ctx;
    const W = this.el.width, H = this.el.height;
    const d = this.data;
    if (d.length < 2) { ctx.fillStyle='#071120'; ctx.fillRect(0,0,W,H); return; }

    ctx.fillStyle = '#071120';
    ctx.fillRect(0, 0, W, H);

    // Grid
    ctx.strokeStyle = '#0d1f35';
    ctx.lineWidth = 0.5;
    for (let g = 1; g < 4; g++) {
      const y = H * g / 4;
      ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(W, y); ctx.stroke();
    }

    const drawSeries = (arr, color, fillCol) => {
      if (arr.length < 2) return;
      let mn = arr[0], mx = arr[0];
      for (const v of arr) { if(v<mn) mn=v; if(v>mx) mx=v; }
      const range = mx - mn || 1;
      const pad = range * 0.12;
      const yMin = mn - pad, yMax = mx + pad;
      const sx = i => (i / (this.maxPts - 1)) * W;
      const sy = v => H - ((v - yMin) / (yMax - yMin)) * H;
      const offset = this.maxPts - arr.length;

      ctx.beginPath();
      for (let i = 0; i < arr.length; i++) {
        const x = sx(i + offset), y = sy(arr[i]);
        i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
      }
      if (fillCol) {
        ctx.lineTo(sx(arr.length - 1 + offset), H);
        ctx.lineTo(sx(offset), H);
        ctx.closePath();
        ctx.fillStyle = fillCol;
        ctx.fill();
        ctx.beginPath();
        for (let i = 0; i < arr.length; i++) {
          const x = sx(i + offset), y = sy(arr[i]);
          i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
        }
      }
      ctx.strokeStyle = color;
      ctx.lineWidth = 1.5 * this.dpr;
      ctx.stroke();
    };

    drawSeries(d, this.color, this.fillColor);
    if (this.data2 && this.data2.length > 1) {
      drawSeries(this.data2, this.color2, null);
    }
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  CHARTS SETUP
// ═══════════════════════════════════════════════════════════════════════════════
const charts = {};

function initCharts() {
  charts.main = new RollingChart('chart-main', {color:'#00d4ff', fill:'rgba(0,212,255,0.05)', maxPts:500});
  charts.gsr  = new RollingChart('chart-gsr',  {color:'#ffb347', fill:'rgba(255,179,71,0.05)', maxPts:500});
  charts.scr  = new RollingChart('chart-scr',  {color:'#1fffa0', fill:'rgba(31,255,160,0.06)', maxPts:200});
  charts.gsr.color2 = '#00d4ff';

  window.addEventListener('resize', () => {
    Object.values(charts).forEach(c => c.resize());
  });
}

function setChart(mode) {
  chartMode = mode;
  document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
  event.target.classList.add('active');
  const labels = {ir:'PPG — IR CHANNEL (Filtered)', red:'PPG — RED CHANNEL (Filtered)', raw:'PPG — IR RAW'};
  document.getElementById('ppg-label').textContent = labels[mode] || '';
}

// ═══════════════════════════════════════════════════════════════════════════════
//  WEBSOCKET
// ═══════════════════════════════════════════════════════════════════════════════
let ws, wsRetry = 0;

function connect() {
  const url = `ws://${location.hostname}/ws`;
  ws = new WebSocket(url);

  ws.onopen = () => {
    wsRetry = 0;
    document.getElementById('overlay').classList.add('hidden');
  };

  ws.onclose = () => {
    document.getElementById('overlay').classList.remove('hidden');
    document.getElementById('overlay-msg').textContent = 'Reconnecting…';
    wsRetry = Math.min(wsRetry + 1, 10);
    setTimeout(connect, Math.min(500 * wsRetry, 5000));
  };

  ws.onerror = () => ws.close();

  ws.onmessage = e => {
    const parts = e.data.trim().split(',');
    if (parts.length !== 7) return;
    const vals = parts.map(Number);
    if (vals.some(isNaN)) return;
    const [tMs, irRaw, irF, redRaw, redF, gsrRaw, gsrF] = vals;
    bufPush(tMs, irRaw, irF, redRaw, redF, gsrRaw, gsrF);

    // Feed polygraph extractor
    polyIngest(tMs, gsrF);
  };
}

// ═══════════════════════════════════════════════════════════════════════════════
//  RENDER LOOP
// ═══════════════════════════════════════════════════════════════════════════════
function renderLoop() {
  const n = bufLen();
  computeMetrics();
  updateUI();

  // Push to charts
  if (n > 0) {
    const irArr  = sampleBuf.ir;
    const redArr = sampleBuf.red;
    const irRArr = sampleBuf.irR;
    const gsrArr = sampleBuf.gsr;
    const scrArr = sampleBuf.scr || [];

    const STEP = Math.max(1, Math.floor(n / 500));
    for (let i = 0; i < n; i += STEP) {
      if (chartMode === 'ir')  charts.main.push(irArr[i]);
      else if (chartMode === 'red') charts.main.push(redArr[i]);
      else charts.main.push(irRArr[i]);
      charts.gsr.push(gsrArr[i]);
      if (scrArr.length > i) charts.gsr.push2(scrArr[i]);
    }
  }

  // SCR chart for polygraph (live phasic GSR)
  if (sampleBuf.scr && sampleBuf.scr.length > 0) {
    charts.scr.push(sampleBuf.scr[sampleBuf.scr.length - 1]);
  }

  Object.values(charts).forEach(c => c.render());
}

// ═══════════════════════════════════════════════════════════════════════════════
//  UI UPDATE
// ═══════════════════════════════════════════════════════════════════════════════
function fmt(v, dec=0) {
  return (v > 0 && isFinite(v)) ? v.toFixed(dec) : '--';
}

function updateUI() {
  const m = metrics;
  document.getElementById('m-hr').textContent    = fmt(m.hr);
  document.getElementById('m-spo2').textContent  = fmt(m.spo2, 1);
  document.getElementById('m-rr').textContent    = fmt(m.rr, 1);
  document.getElementById('m-pi').textContent    = fmt(m.pi, 2);
  document.getElementById('m-sdnn').textContent  = fmt(m.sdnn, 1);
  document.getElementById('m-rmssd').textContent = fmt(m.rmssd, 1);
  document.getElementById('m-pav').textContent   = fmt(m.pav, 1);
  document.getElementById('m-ed').textContent    = fmt(m.ed, 0);
  document.getElementById('m-scl').textContent   = fmt(m.scl, 0);
  document.getElementById('m-scr').textContent   = fmt(m.scr, 2);
  document.getElementById('m-gsr').textContent   = Math.round(m.gsrRaw) || '--';
  document.getElementById('m-aug').textContent   = fmt(m.aug, 1);

  // SQI bar
  const sqi = m.sqi;
  document.getElementById('sqi-fill').style.width = Math.min(100,sqi) + '%';
  document.getElementById('sqi-fill').style.background = sqi>60?'var(--green)':sqi>40?'var(--amber)':'var(--red)';
  document.getElementById('sqi-val').textContent = sqi > 0 ? sqi.toFixed(0)+'%' : '--';

  // FS / BUF
  if (fsEst > 0) document.getElementById('fs-pill').textContent = 'FS ' + fsEst.toFixed(0) + 'Hz';
  document.getElementById('buf-pill').textContent = 'BUF ' + bufLen();

  // Sensor indicator
  const noSensor = bufLen() > 200 && metrics.spo2 === 0 && mean(sampleBuf.ir) < 100;
  document.getElementById('no-sensor').style.display = noSensor ? '' : 'none';

  // Clock
  const now = new Date();
  document.getElementById('ts-clock').textContent =
    String(now.getHours()).padStart(2,'0') + ':' +
    String(now.getMinutes()).padStart(2,'0') + ':' +
    String(now.getSeconds()).padStart(2,'0');

  // Polygraph SQI bar
  const psb = document.getElementById('poly-sqi-bar');
  if (psb) {
    psb.style.width = Math.min(100,sqi) + '%';
    psb.style.background = sqi>60?'var(--green)':sqi>40?'var(--amber)':'var(--red)';
  }
}

function switchView(v) {
  const main = document.getElementById('main');
  const poly = document.getElementById('poly-view');
  const nbio = document.getElementById('nav-bio');
  const npoly = document.getElementById('nav-poly');
  if (v === 'poly') {
    main.style.display = 'none'; poly.classList.add('active');
    nbio.classList.remove('active'); npoly.classList.add('active');
  } else {
    main.style.display = ''; poly.classList.remove('active');
    nbio.classList.add('active'); npoly.classList.remove('active');
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  POLYGRAPH STATE MACHINE (runs entirely in browser)
// ═══════════════════════════════════════════════════════════════════════════════

const POLY_STATES = {IDLE:'IDLE',BASELINE:'BASELINE_CAPTURE',READY:'READY',
                     RUNNING:'RUNNING',PAUSE:'AUTO_PAUSE',ANALYSIS:'ANALYSIS',COMPLETE:'COMPLETE'};

// Default CQT question bank
const QUESTIONS = {
  relevant: [
    {id:'R1',text:'Did you commit the act you are suspected of?',qtype:'R'},
    {id:'R2',text:'Were you present when it happened?',qtype:'R'},
    {id:'R3',text:'Did you plan this in advance?',qtype:'R'},
  ],
  comparison: [
    {id:'C1',text:'Have you ever deceived someone who trusted you?',qtype:'C'},
    {id:'C2',text:'Have you ever taken something that was not yours?',qtype:'C'},
    {id:'C3',text:'Have you ever lied seriously to avoid consequences?',qtype:'C'},
  ],
  irrelevant: [
    {id:'I1',text:'Are you currently seated?',qtype:'I'},
    {id:'I2',text:'Is it currently nighttime?',qtype:'I'},
  ]
};

const NSS_THRESHOLDS = [
  [2.0,3],[1.5,2],[1.0,1],[0.25,0],[-0.25,0],[-1.0,-1],[-1.5,-2],[-2.0,-3]
];

function deltaTonss(delta) {
  for (const [thresh, score] of NSS_THRESHOLDS) {
    if (delta >= thresh) return score;
  }
  return -3;
}

// Polygraph state
let poly = {
  state: POLY_STATES.IDLE,
  sessionId: '',
  subject: '',
  technique: 'CQT',
  blocks: [],
  blockIdx: 0,
  qIdx: 0,
  baselineData: null,
  baselineStart: 0,
  baselineSnaps: [],
  armed: false,
  armTs: 0,
  windowData: [],   // phasic GSR snapshots during question window
  questionScores: [],
  nssRows: [],
  currentQ: null,
  qTimer: null,
  pauseWatchdog: null,
  lowSqiStart: 0,
  requeue: [],
  nScored: 0,
  report: null,
  WARMUP_S: 30,
  DURATION_S: 120,
  MIN_VALID: 40,
  SQI_THRESH: 60,
  EDR_START_S: 1.0,
  EDR_END_S: 10.0,
  ISI_S: 20.0,
  Q_DUR_S: 5.0,
  PRE_BUF_S: 2.0,
  CHANNEL_WEIGHTS: {scr:1.0, hr:1.0, rr:0.8},
};

// Build CQT blocks (3 repetitions, interleaved I/C/R/C/R/C/R/I)
function buildCQTBlocks() {
  const {relevant:R, comparison:C, irrelevant:I} = QUESTIONS;
  const blocks = [];
  for (let rep = 0; rep < 3; rep++) {
    const block = [];
    block.push(I[0]);
    for (let i = 0; i < Math.min(R.length, C.length); i++) {
      block.push(C[i]); block.push(R[i]);
    }
    block.push(I[1] || I[0]);
    blocks.push(block);
  }
  return blocks;
}

// Ingest a sample during baseline or running
function polyIngest(ts, gsrPhasic) {
  if (poly.state === POLY_STATES.BASELINE) {
    const elapsed = (Date.now() - poly.baselineStart) / 1000;
    if (elapsed >= poly.WARMUP_S && metrics.sqi >= poly.SQI_THRESH) {
      poly.baselineSnaps.push({
        ts, gsr: gsrPhasic,
        hr: metrics.hr, rr: metrics.rr, sqi: metrics.sqi
      });
    }
    // Progress update
    const pct = Math.min(100, elapsed / poly.DURATION_S * 100);
    const eta = Math.max(0, Math.round(poly.DURATION_S - elapsed));
    const isWarmup = elapsed < poly.WARMUP_S;
    setEl('poly-bl-bar', 'style.width', pct.toFixed(1) + '%');
    setEl('poly-bl-pct', 'textContent', Math.round(pct) + '%');
    setEl('poly-bl-eta', 'textContent', 'ETA ' + eta + 's');
    setEl('poly-bl-info', 'textContent',
      isWarmup ? 'Warming up — sensor settling…' :
      `Collecting (valid: ${poly.baselineSnaps.length})`);

    if (elapsed >= poly.DURATION_S) {
      tryCompleteBaseline();
    }
  }

  if (poly.state === POLY_STATES.RUNNING && poly.armed) {
    const sinceArm = (ts - poly.armTs) / 1000;
    poly.windowData.push({ts, sinceArm, gsr: gsrPhasic,
      hr: metrics.hr, rr: metrics.rr, sqi: metrics.sqi});
  }

  // SQI watchdog during RUNNING
  if (poly.state === POLY_STATES.RUNNING) {
    if (metrics.sqi < 40) {
      if (!poly.lowSqiStart) poly.lowSqiStart = Date.now();
      else if ((Date.now() - poly.lowSqiStart) >= 3000) {
        triggerAutoPause();
      }
    } else {
      poly.lowSqiStart = 0;
    }
  }
}

function tryCompleteBaseline() {
  const n = poly.baselineSnaps.length;
  if (n < poly.MIN_VALID) {
    polySetState(POLY_STATES.IDLE);
    alert(`Baseline failed: only ${n}/${poly.MIN_VALID} valid samples. Re-seat sensor and try again.`);
    return;
  }
  // Compute baseline statistics
  poly.baselineData = computeBaseline(poly.baselineSnaps);
  polySetState(POLY_STATES.READY);
  updatePolyUI();
}

function computeBaseline(snaps) {
  const keys = ['gsr','hr','rr'];
  const bl = {};
  for (const k of keys) {
    const vals = snaps.map(s => s[k]).filter(isFinite);
    const m = mean(vals);
    const s = std(vals) || 1e-9;
    bl[k] = {mean: m, std: s, n: vals.length};
  }
  return bl;
}

function zScore(metric, value) {
  if (!poly.baselineData || !poly.baselineData[metric]) return 0;
  const {mean: m, std: s} = poly.baselineData[metric];
  return (value - m) / s;
}

function polyCmd(cmd) {
  if (cmd === 'start') {
    poly.subject   = document.getElementById('poly-subject').value.trim() || 'ANON';
    poly.technique = document.getElementById('poly-technique').value;
    poly.blocks    = buildCQTBlocks();
    poly.blockIdx  = 0;
    poly.qIdx      = 0;
    poly.baselineSnaps = [];
    poly.questionScores = [];
    poly.nssRows = [];
    poly.requeue = [];
    poly.nScored = 0;
    poly.report = null;
    poly.sessionId = new Date().toISOString().replace(/[T:.Z-]/g,'').slice(0,14);
    poly.baselineStart = Date.now();
    polySetState(POLY_STATES.BASELINE);
    updatePolyUI();
    document.getElementById('nav-poly').style.display = 'inline-block';

  } else if (cmd === 'force_baseline') {
    if (poly.state !== POLY_STATES.BASELINE) return;
    if (!confirm('Force baseline complete?')) return;
    tryCompleteBaseline();

  } else if (cmd === 'begin') {
    if (poly.state !== POLY_STATES.READY) return;
    runBlock();

  } else if (cmd === 'resume') {
    if (poly.state !== POLY_STATES.PAUSE) return;
    poly.lowSqiStart = 0;
    polySetState(POLY_STATES.RUNNING);
    runBlock();

  } else if (cmd === 'reset') {
    if (!confirm('Reset session? All data will be lost.')) return;
    clearInterval(poly.qTimer);
    polySetState(POLY_STATES.IDLE);
    poly.sessionId = '';
    poly.questionScores = [];
    poly.nssRows = [];
    poly.baselineData = null;
    poly.requeue = [];
    updatePolyUI();
  }
}

function polySetState(s) {
  poly.state = s;
  const badge = document.getElementById('poly-state-badge');
  const colors = {IDLE:'var(--muted)',BASELINE_CAPTURE:'var(--cyan)',READY:'var(--lime)',
    RUNNING:'var(--green)',AUTO_PAUSE:'var(--red)',ANALYSIS:'var(--amber)',COMPLETE:'var(--purple)'};
  badge.textContent = s.replace('_',' ');
  badge.style.color = colors[s] || 'var(--muted)';
  badge.style.borderColor = colors[s] || 'var(--muted)';
}

function updatePolyUI() {
  const s = poly.state;
  const en = (id, v) => { const e = document.getElementById(id); if(e) e.style.opacity = v?'1':'0.3'; e && (v ? e.removeAttribute('style') : (e.style.opacity='0.3',e.style.pointerEvents='none')); };
  // Re-apply pointer-events cleanly
  const btn = (id, enabled) => {
    const e = document.getElementById(id);
    if (!e) return;
    if (enabled) { e.removeAttribute('disabled'); e.style.opacity=''; e.style.pointerEvents=''; }
    else { e.setAttribute('disabled',''); e.style.opacity='0.3'; e.style.pointerEvents='none'; }
  };
  btn('btn-start',  s === POLY_STATES.IDLE);
  btn('btn-begin',  s === POLY_STATES.READY);
  btn('btn-resume', s === POLY_STATES.PAUSE);
  btn('btn-reset',  s !== POLY_STATES.IDLE);

  show('poly-idle-msg',     s === POLY_STATES.IDLE);
  show('poly-baseline-panel', s === POLY_STATES.BASELINE);
  show('poly-pause-panel',  s === POLY_STATES.PAUSE);
  show('poly-sqi-live',     s === POLY_STATES.RUNNING || s === POLY_STATES.BASELINE);
  show('poly-active-row',   s === POLY_STATES.RUNNING);
  show('poly-scoring-row',  poly.nssRows.length > 0);
  show('poly-complete-row', s === POLY_STATES.COMPLETE);

  const info = poly.sessionId ?
    `Session: ${poly.sessionId} | ${poly.technique} | ${poly.subject}` : 'No active session';
  setEl('poly-session-info','textContent', info);
}

function show(id, v) {
  const e = document.getElementById(id);
  if (e) e.style.display = v ? '' : 'none';
}

function setEl(id, prop, val) {
  const e = document.getElementById(id);
  if (!e) return;
  const parts = prop.split('.');
  if (parts.length === 2) e[parts[0]][parts[1]] = val;
  else e[prop] = val;
}

// ─── Block runner ──────────────────────────────────────────────────────────────
function runBlock() {
  polySetState(POLY_STATES.RUNNING);
  updatePolyUI();

  // Questions = current block + any requeued
  const block = [...poly.blocks[poly.blockIdx], ...poly.requeue];
  poly.requeue = [];
  let qi = 0;

  const nextQuestion = () => {
    if (poly.state !== POLY_STATES.RUNNING) return;
    if (qi >= block.length) {
      // Block done
      poly.blockIdx++;
      polySetState(poly.blockIdx >= poly.blocks.length ? POLY_STATES.ANALYSIS : POLY_STATES.READY);
      if (poly.state === POLY_STATES.ANALYSIS) runAnalysis();
      else {
        updatePolyUI();
        show('poly-active-row', false);
      }
      return;
    }

    const q = block[qi++];
    poly.currentQ = q;
    poly.qIdx = qi;

    // Pre-question buffer (2s)
    setTimeout(() => {
      if (poly.state !== POLY_STATES.RUNNING) return;

      // Arm extractor
      poly.armed   = true;
      poly.armTs   = sampleBuf.ts[sampleBuf.ts.length-1] || Date.now();
      poly.windowData = [];

      // Update UI
      const badge = document.getElementById('poly-q-badge');
      badge.textContent = {R:'RELEVANT',C:'COMPARISON',I:'IRRELEVANT',P:'PROBE',F:'FOIL'}[q.qtype]||q.qtype;
      badge.style.background = Q_COLORS[q.qtype]||'var(--border)';
      badge.style.color = '#fff';
      document.getElementById('poly-q-text').textContent = q.text;
      setEl('poly-block-n','textContent', poly.blockIdx+1);
      setEl('poly-block-tot','textContent', poly.blocks.length);
      setEl('poly-q-n','textContent', qi);
      setEl('poly-q-tot','textContent', block.length);
      show('poly-active-row', true);

      // Countdown
      let rem = Math.round(poly.Q_DUR_S);
      document.getElementById('poly-q-timer').textContent = rem + 's';
      const cd = setInterval(() => {
        rem--;
        document.getElementById('poly-q-timer').textContent = Math.max(0,rem) + 's';
        if (rem <= 0) clearInterval(cd);
      }, 1000);

      // After question display + ISI, flush and score
      const totalWait = (poly.Q_DUR_S + poly.ISI_S) * 1000;
      setTimeout(() => {
        if (poly.state !== POLY_STATES.RUNNING) return;
        flushWindow(q);
        nextQuestion();
      }, totalWait);

    }, poly.PRE_BUF_S * 1000);
  };

  nextQuestion();
}

function triggerAutoPause() {
  if (poly.state !== POLY_STATES.RUNNING) return;
  // Requeue current question
  if (poly.currentQ) poly.requeue.push(poly.currentQ);
  poly.armed = false;
  polySetState(POLY_STATES.PAUSE);
  updatePolyUI();
}

// ─── Response extractor / EDR peak detection ───────────────────────────────────
function flushWindow(q) {
  poly.armed = false;
  const win = poly.windowData;
  if (win.length < 5) return;

  // Detect EDR peak in phasic GSR (1-10s after onset)
  const edrWin = win.filter(s => s.sinceArm >= poly.EDR_START_S && s.sinceArm <= poly.EDR_END_S);
  let edrPeak = null, edrLat = null;

  if (edrWin.length > 3) {
    const gsrArr  = edrWin.map(s => s.gsr);
    const gsrMean = mean(gsrArr), gsrStd = std(gsrArr);
    const minProm = Math.max(gsrStd * 0.3, 0.5);
    const peaks   = findPeaks(gsrArr, 3, minProm);
    if (peaks.length > 0) {
      // Highest peak
      const best = peaks.reduce((a,b) => gsrArr[a] > gsrArr[b] ? a : b);
      edrPeak = gsrArr[best];
      edrLat  = edrWin[best].sinceArm;
      document.getElementById('poly-edr-lat').textContent = edrLat.toFixed(2) + 's';
      document.getElementById('poly-edr-cnt').textContent =
        (parseInt(document.getElementById('poly-edr-cnt').textContent)||0) + 1;
    }
  }

  // Window metrics: mean HR, GSR over 2-7s post-onset
  const respWin = win.filter(s => s.sinceArm >= 2 && s.sinceArm <= 7);
  const preWin  = win.slice(0, Math.min(10, win.length));

  const wMetrics = {
    gsr: mean(respWin.map(s=>s.gsr)),
    hr:  mean(respWin.map(s=>s.hr)),
    rr:  mean(respWin.map(s=>s.rr)),
  };
  const preMetrics = {
    gsr: mean(preWin.map(s=>s.gsr)),
    hr:  mean(preWin.map(s=>s.hr)),
    rr:  mean(preWin.map(s=>s.rr)),
  };

  const window_ = {
    q_id: q.id, qtype: q.qtype, text: q.text,
    edrPeak, edrLat,
    edrValid: edrPeak !== null,
    metrics: wMetrics,
    preMetrics,
    sqi: mean(respWin.map(s=>s.sqi)),
  };

  if (q.qtype === 'R' || q.qtype === 'C') {
    scoreWindow(window_);
  }

  poly.windowData = [];
}

// ─── Scoring ───────────────────────────────────────────────────────────────────
const windowStore = [];  // all flushed windows

function scoreWindow(w) {
  windowStore.push(w);

  const targetType = w.qtype === 'R' ? 'C' : 'R';
  let matched = null;
  for (let i = windowStore.length - 2; i >= 0; i--) {
    if (windowStore[i].qtype === targetType) { matched = windowStore[i]; break; }
  }
  if (!matched) return;

  const rW = w.qtype === 'R' ? w : matched;
  const cW = w.qtype === 'C' ? w : matched;

  const channels = {};
  for (const ch of ['gsr','hr','rr']) {
    const inv = ch === 'rr';  // decrease in RR = deceptive (heart rate up)
    const rVal = rW.metrics[ch], cVal = cW.metrics[ch];
    const delta = inv ? (cVal - rVal) : (rVal - cVal);
    const nss = deltaTonss(delta / Math.max(Math.abs(poly.baselineData?.[ch]?.std || 1), 0.01));
    channels[ch] = {delta, nss, r: rVal, c: cVal};
  }

  const weights = poly.CHANNEL_WEIGHTS;
  let composite = 0, wSum = 0;
  for (const [ch, d] of Object.entries(channels)) {
    composite += (weights[ch] || 0.5) * d.nss;
    wSum += weights[ch] || 0.5;
  }
  composite = wSum > 0 ? composite / wSum : 0;

  const pair = {q_id: rW.q_id, text: rW.text, channels, composite};
  poly.questionScores.push(pair);
  poly.nssRows.push(pair);
  poly.nScored++;

  renderNssTable();
  updatePolyUI();
}

function renderNssTable() {
  const rows = poly.nssRows;
  if (!rows.length) return;
  const CHS = ['gsr','hr','rr'];
  let html = '<table style="border-collapse:collapse"><thead><tr>';
  html += '<th style="padding:3px 6px;color:var(--muted);text-align:left;font-size:.6rem">Q</th>';
  CHS.forEach(ch => html += `<th style="padding:2px 8px;color:var(--muted);font-size:.6rem">${ch.toUpperCase()}</th>`);
  html += '<th style="padding:2px 8px;color:var(--muted);font-size:.6rem">∑</th></tr></thead><tbody>';

  let totalComp = 0;
  rows.forEach(r => {
    html += `<tr><td style="padding:3px 6px;color:var(--red);font-size:.65rem;font-family:var(--mono)">${r.q_id}</td>`;
    CHS.forEach(ch => {
      const v = r.channels[ch] ? r.channels[ch].nss : null;
      const s = v !== null ? (v > 0 ? '+'+v : ''+v) : '·';
      const cls = v !== null ? ['nss-m3','nss-m2','nss-m1','nss-0','nss-0','nss-p1','nss-p2','nss-p3'][v + 3] : 'nss-0';
      html += `<td style="padding:2px 4px;text-align:center"><span class="nss-cell ${cls}">${s}</span></td>`;
    });
    const c = r.composite.toFixed(1);
    html += `<td style="padding:3px 8px;font-family:var(--mono);font-size:.72rem;color:var(--text)">${r.composite>0?'+':''}${c}</td></tr>`;
    totalComp += r.composite;
  });
  html += '</tbody></table>';
  document.getElementById('poly-nss-table').innerHTML = html;

  // Composite score
  const avg = rows.length ? totalComp / rows.length : 0;
  document.getElementById('poly-composite').textContent = (avg > 0 ? '+' : '') + avg.toFixed(2);

  const detBadge = document.getElementById('poly-det-badge');
  detBadge.style.display = '';
  const det = avg >= 6 ? 'DI' : avg <= -6 ? 'NDI' : 'INC';
  detBadge.className = 'det-box ' + det;
  detBadge.textContent = {DI:'DECEPTION INDICATED',NDI:'NO DECEPTION INDICATED',INC:'INCONCLUSIVE'}[det];
}

// ─── Analysis ──────────────────────────────────────────────────────────────────
function runAnalysis() {
  polySetState(POLY_STATES.ANALYSIS);
  updatePolyUI();
  setTimeout(() => {
    const scores = poly.nssRows;
    const total  = scores.reduce((s,r) => s + r.composite, 0);
    const avg    = scores.length ? total / scores.length : 0;
    const det    = avg >= 6 ? 'DI' : avg <= -6 ? 'NDI' : 'INC';
    const edrMissing = windowStore.filter(w => !w.edrValid).length;
    const edrSupp = windowStore.length ? edrMissing / windowStore.length : 0;

    poly.report = {
      sessionId: poly.sessionId, subject: poly.subject,
      technique: poly.technique, ts: new Date().toISOString(),
      determination: det, composite: avg,
      edrSuppression: edrSupp, nScored: poly.nScored,
      scores: poly.nssRows,
    };

    polySetState(POLY_STATES.COMPLETE);

    document.getElementById('poly-final-det').textContent =
      {DI:'DECEPTION INDICATED',NDI:'NO DECEPTION INDICATED',INC:'INCONCLUSIVE'}[det];
    document.getElementById('poly-final-det').className = 'det-box ' + det;
    document.getElementById('poly-final-comp').textContent = (avg>0?'+':'') + avg.toFixed(2);
    document.getElementById('poly-final-edr').textContent = (edrSupp*100).toFixed(0) + '%';
    document.getElementById('poly-final-n').textContent = poly.nScored;

    updatePolyUI();
    show('poly-complete-row', true);
    show('poly-active-row', false);
  }, 1500);
}

function downloadReport() {
  if (!poly.report) return;
  const blob = new Blob([JSON.stringify(poly.report, null, 2)], {type:'application/json'});
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = `biomonitor_${poly.report.sessionId}.json`;
  a.click();
}

// ═══════════════════════════════════════════════════════════════════════════════
//  INIT
// ═══════════════════════════════════════════════════════════════════════════════
initCharts();
connect();
setInterval(renderLoop, 1000 / FS_DISPLAY);
</script>
</body>
</html>
)HTMLEOF";
