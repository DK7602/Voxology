// Voxology UI. Talks to the plug-in through JUCE's web relays (parameters), the "voxMeters"
// event (30 Hz) and four native functions (Auto-Edit).

import * as Juce from "./juce/index.js";

const $ = (id) => document.getElementById(id);
const MINUS = "−";
const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
const fmtNum = (v, d = 1) => (v < 0 && Math.abs(v) >= 0.5 * Math.pow(10, -d) ? MINUS : "") + Math.abs(v).toFixed(d);
const fmtDb = (v, d = 1) => `${fmtNum(v, d)} dB`;
const fmtSigned = (v, d = 1) => `${v >= 0.05 ? "+" : ""}${fmtNum(v, d)} dB`;
const fmtHz = (f) => (f >= 1000 ? `${(f / 1000).toFixed(1)} kHz` : `${Math.round(f)} Hz`);
const fmtPct = (v) => `${Math.round(v)} %`;
const fmtMs = (v) => `${Math.round(v)} ms`;

// ---------------------------------------------------------------------------------------------
// Scale the fixed 1600x900 design to the window.
const stage = $("stage");
function fit() { stage.style.transform = `scale(${Math.min(window.innerWidth / 1600, window.innerHeight / 900)})`; }
window.addEventListener("resize", fit);
fit();

// ---------------------------------------------------------------------------------------------
// Parameters
const SLIDERS = ["ptAmount", "ptSpeed", "ptHumanize", "clLowCut", "clGateThr", "clGateRange", "dsAmount", "dsSens", "dsFreq", "rdTarget", "rdRange",
  "cpPeak", "cpThr", "cpRatio", "cpMakeup", "cpMix", "saDrive", "saMix", "dbAmount", "dbWidth",
  "dlFeedback", "dlMix", "dlTone", "dlDuck", "rvDecay", "rvPredelay", "rvMix", "rvTone", "rvDuck", "outGain",
  ...[1, 2, 3, 4, 5].flatMap((b) => ["eqGain" + b, "eqFreq" + b]), "dqSens", ...[1, 2, 3, 4].flatMap((b) => ["dqCut" + b, "dqFreq" + b])];
const TOGGLES = ["bypass", "listenA", "levelMatch", "ptOn", "clOn", "eqOn", "dqOn", "dsOn", "rdOn", "cpOn", "saOn", "dbOn", "dlOn", "dlPing", "rvOn"];
const COMBOS = ["aeStyle", "aeIntensity", "ptKey", "ptScale", "rdSpeed", "saMode", "dlTime"];
const P = {};
for (const id of SLIDERS) P[id] = Juce.getSliderState(id);
for (const id of TOGGLES) P[id] = Juce.getToggleState(id);
for (const id of COMBOS) P[id] = Juce.getComboBoxState(id);
const val = (id) => P[id].getScaledValue();
const on = (id) => !!P[id].getValue();
const choice = (id) => P[id].getChoiceIndex();

const aeStart = Juce.getNativeFunction("startAutoEdit");
const aeCancel = Juce.getNativeFunction("cancelAutoEdit");
const aeUndo = Juce.getNativeFunction("undoAutoEdit");
const aeGetReport = Juce.getNativeFunction("getAutoEditReport");

function scaledToNorm(state, v) {
  const { start, end, skew } = state.properties;
  return Math.pow(clamp((v - start) / (end - start), 0, 1), skew);
}
function setScaled(id, v) { P[id].setNormalisedValue(scaledToNorm(P[id], v)); }

// Latest meter frame.
const M = { pitchSung: 0, pitchTarget: -1, pitchCorr: 0, inShort: -100, outShort: -100, inPeak: -100, outPeak: -100, gate: 0, dyn: [0, 0, 0, 0], deEss: 0, rider: 0, peakGr: 0, levelGr: 0,
  satHarm: -100, matchDb: 0, bpm: 0, sr: 48000, aeState: 0, aeProgress: 0, aeHearing: false, aeUndo: false, aeReport: 0, in: null, out: null };
let report = null;          // last Auto-Edit report (parsed) or null
let learnTab = "module";

// ---------------------------------------------------------------------------------------------
// Modules
const STYLES = ["Trap Lead", "Rap", "Melodic", "Ad-libs", "R&B"];
const INTENSITIES = ["Light", "Balanced", "Strong"];
const DELAYS = ["1/4", "1/8", "1/8 dot", "1/4 dot", "1/16", "1/2"];
const EQ_BANDS = [
  { name: "Body", type: 0, q: 0.7 }, { name: "Mud", type: 1, q: 1.4 }, { name: "Nasal", type: 1, q: 1.6 },
  { name: "Presence", type: 1, q: 0.9 }, { name: "Air", type: 2, q: 0.7 },
];
const DYN_BANDS = [
  { name: "Boom", short: "B", q: 1.0, def: 150 }, { name: "Mud", short: "M", q: 1.4, def: 350 },
  { name: "Nasal", short: "N", q: 1.6, def: 1000 }, { name: "Harsh", short: "H", q: 1.4, def: 3500 },
];
const dynCut = (b) => val("dqCut" + (b + 1));
const dynFreq = (b) => val("dqFreq" + (b + 1));
const dynLive = (b) => (M.dyn && Number.isFinite(M.dyn[b]) ? M.dyn[b] : 0);   // dB, <= 0
const fmtCut = (v) => (v < 0.05 ? "Off" : `${MINUS}${v.toFixed(1)} dB`);
const eqGain = (b) => val("eqGain" + (b + 1));
const eqFreq = (b) => val("eqFreq" + (b + 1));
const compNeutral = () => val("cpPeak") > -0.05 && val("cpRatio") < 1.005 && Math.abs(val("cpMakeup")) < 0.005;

const NOTES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
const SCALES = ["Chromatic", "Major", "Minor", "Harmonic Minor", "Minor Pentatonic", "Major Pentatonic"];
const SCALES_SHORT = ["Chromatic", "Major", "Minor", "Harm. Minor", "Minor Penta", "Major Penta"];
const noteName = (m) => `${NOTES[((Math.round(m) % 12) + 12) % 12]}${Math.floor(Math.round(m) / 12) - 1}`;

/** Live pitch: the note you sing (with how far off, in cents) and the note it pulls to. */
function pitchCell() {
  const cell = document.createElement("div");
  cell.className = "cell pitch-cell";
  cell.innerHTML = `<span class="cell-label">Tune</span><span class="cell-sub">you sing \u2192 you get</span>
    <div class="pitch-face"><div class="pf-row"><span class="pf-sung">\u2014</span><span class="pf-arrow">\u2192</span><span class="pf-target">\u2014</span></div>
    <div class="pf-bar"><i class="pf-zero"></i><i class="pf-dot"></i></div><div class="pf-cents mono">listening</div></div>`;
  const sung = cell.querySelector(".pf-sung"), tgt = cell.querySelector(".pf-target"), dot = cell.querySelector(".pf-dot"), c = cell.querySelector(".pf-cents");
  cell.update = () => {
    if (!M.pitchSung || M.pitchTarget < 0) { sung.textContent = "\u2014"; tgt.textContent = "\u2014"; c.textContent = "no note"; dot.style.left = "50%"; dot.classList.add("idle"); return; }
    const off = (M.pitchSung - M.pitchTarget) * 100;
    sung.textContent = noteName(M.pitchSung);
    tgt.textContent = noteName(M.pitchTarget);
    dot.classList.remove("idle");
    dot.style.left = `${50 + clamp(off, -50, 50)}%`;
    c.textContent = `${off >= 0 ? "+" : MINUS}${Math.abs(off).toFixed(0)}\u00A2 \u00B7 fix ${Math.abs(M.pitchCorr * 100).toFixed(0)}\u00A2`;
  };
  liveMeters.push(cell);
  return cell;
}

const MODULES = [
  { key: "pitch", name: "PITCH", onId: "ptOn", what: "auto-tune: pulls every note onto your key",
    cells: () => [
      knob("ptAmount", "Amount", "how much", fmtPct, 0),
      knob("ptSpeed", "Retune", "low = robotic", fmtMs, 50),
      knob("ptHumanize", "Humanize", "long notes live", fmtPct, 0),
      grid("ptKey", "Key", "your beat's key", NOTES, 4),
      grid("ptScale", "Scale", "allowed notes", SCALES_SHORT, 1),
      pitchCell(),
    ],
    stat: () => (val("ptAmount") < 0.05 ? ["idle", false] : M.pitchTarget >= 0 ? [`\u2192 ${noteName(M.pitchTarget)}`, true]
      : [`${NOTES[choice("ptKey")]} ${SCALES_SHORT[choice("ptScale")].split(" ")[0].toLowerCase()}`, true]) },
  { key: "cleanup", name: "CLEANUP", onId: "clOn", what: "low cut + gate: rumble and room noise out",
    cells: () => [
      knob("clLowCut", "Low Cut", "rumble below", (v) => (v <= 20.5 ? "Off" : fmtHz(v)), 20),
      knob("clGateThr", "Threshold", "gate opens above", fmtDb, -60),
      knob("clGateRange", "Range", "gaps turned down", (v) => (v < 0.05 ? "Off" : fmtDb(v)), 0),
      meter("Gate", "turning down now", () => M.gate, 30, false, (v) => (v > -0.1 ? "open" : fmtDb(v))),
    ],
    stat: () => (M.gate < -0.5 ? [`gate ${fmtNum(M.gate, 0)} dB`, true] : val("clLowCut") > 20.5 || val("clGateRange") >= 0.05
      ? [val("clLowCut") > 20.5 ? `cut ${fmtHz(val("clLowCut"))}` : "gate ready", true] : ["idle", false]) },
  { key: "eq", name: "TONE EQ", onId: "eqOn", what: "body, mud, nasal, presence, air",
    cells: () => [
      ...EQ_BANDS.map((b, i) => knob("eqGain" + (i + 1), b.name, "gain", fmtSigned, 0, true)),
      ...EQ_BANDS.map((b, i) => knob("eqFreq" + (i + 1), b.name, "frequency", fmtHz, [180, 300, 900, 4000, 12000][i])),
    ],
    stat: () => { const n = [0, 1, 2, 3, 4].filter((b) => Math.abs(eqGain(b)) >= 0.05).length; return n ? [`${n} band${n > 1 ? "s" : ""}`, true] : ["flat", false]; } },
  { key: "dyneq", name: "DYNAMIC EQ", onId: "dqOn", what: "cuts boom, mud, honk and harshness only when they jump out",
    cells: () => [
      ...DYN_BANDS.map((b, i) => knob("dqCut" + (i + 1), b.name, "max cut", fmtCut, 0)),
      dynMeter(),
      ...DYN_BANDS.map((b, i) => knob("dqFreq" + (i + 1), b.name, "frequency", fmtHz, b.def)),
      knob("dqSens", "Sensitivity", "how easily", fmtPct, 50),
    ],
    stat: () => {
      const n = [0, 1, 2, 3].filter((b) => dynCut(b) >= 0.05).length;
      if (!n) return ["idle", false];
      const deepest = Math.min(...[0, 1, 2, 3].map(dynLive));
      return deepest < -0.3 ? [fmtDb(deepest), true] : [`${n} band${n > 1 ? "s" : ""}`, true];
    } },
  { key: "deess", name: "DE-ESSER", onId: "dsOn", what: "tames sharp s, t, sh and ch sounds",
    cells: () => [
      knob("dsAmount", "Amount", "how hard", fmtPct, 0),
      knob("dsSens", "Sensitivity", "how easily", fmtPct, 50),
      knob("dsFreq", "Frequency", "where s lives", fmtHz, 6000),
      meter("Cut", "on this s", () => M.deEss, 12, false, (v) => (v > -0.1 ? "0 dB" : fmtDb(v))),
    ],
    stat: () => (val("dsAmount") < 0.05 ? ["idle", false] : M.deEss < -0.3 ? [fmtDb(M.deEss), true] : ["ready", true]) },
  { key: "rider", name: "RIDER", onId: "rdOn", what: "rides the level like a fader, word by word",
    cells: () => [
      knob("rdTarget", "Target", "level to ride to", fmtDb, -20),
      knob("rdRange", "Range", "most it moves", (v) => (v < 0.05 ? "Off" : `±${fmtNum(v)} dB`), 0),
      seg("rdSpeed", "Speed", "how fast", ["Slow", "Med", "Fast"]),
      meter("Gain", "riding now", () => M.rider, 12, null, fmtSigned),
    ],
    stat: () => (val("rdRange") < 0.05 ? ["idle", false] : [fmtSigned(M.rider), true]) },
  { key: "comp", name: "COMPRESSOR", onId: "cpOn", what: "evens the performance: peak catcher + smooth leveler",
    cells: () => [
      knob("cpPeak", "Peak", "catch spikes", (v) => (v > -0.05 ? "Off" : fmtDb(v)), 0),
      knob("cpThr", "Threshold", "smooth leveler", fmtDb, 0),
      knob("cpRatio", "Ratio", "how firm", (v) => `${v.toFixed(1)}:1`, 1),
      knob("cpMakeup", "Makeup", "level back up", fmtSigned, 0),
      knob("cpMix", "Mix", "parallel blend", fmtPct, 100),
      meter("Reduction", "peak + level", () => M.peakGr + M.levelGr, 18, false, (v) => (v > -0.1 ? "0 dB" : fmtDb(v))),
    ],
    stat: () => (compNeutral() ? ["idle", false] : [fmtDb(M.peakGr + M.levelGr), true]) },
  { key: "sat", name: "SATURATION", onId: "saOn", what: "harmonics: warmth, density, presence",
    cells: () => [
      seg("saMode", "Mode", "character", ["Tape", "Tube", "Clip"]),
      knob("saDrive", "Drive", "how much", fmtDb, 0),
      knob("saMix", "Mix", "blend", fmtPct, 50),
      meter("Harmonics", "added now", () => M.satHarm, 60, true, (v) => (v <= -99 ? "none" : fmtDb(v)), -70),
    ],
    stat: () => (val("saDrive") < 0.05 || val("saMix") < 0.05 ? ["idle", false] : [["Tape", "Tube", "Clip"][choice("saMode")] + ` ${fmtNum(val("saDrive"), 0)} dB`, true]) },
  { key: "double", name: "DOUBLER", onId: "dbOn", what: "a stacked double take, left and right",
    cells: () => [knob("dbAmount", "Amount", "how loud", fmtPct, 0), knob("dbWidth", "Width", "how far apart", fmtPct, 70)],
    stat: () => (val("dbAmount") < 0.05 ? ["idle", false] : [fmtPct(val("dbAmount")), true]) },
  { key: "delay", name: "DELAY", onId: "dlOn", what: "echoes locked to your song's tempo",
    cells: () => [
      vseg("dlTime", "Time", "on the beat", DELAYS),
      knob("dlFeedback", "Feedback", "repeats", fmtPct, 25),
      knob("dlMix", "Mix", "echo level", fmtPct, 0),
      knob("dlTone", "Tone", "darker repeats", fmtHz, 6000),
      knob("dlDuck", "Duck", "quiet while singing", fmtPct, 50),
      toggle("dlPing", "Ping-Pong", "bounce L / R", "ON", "OFF"),
    ],
    stat: () => (val("dlMix") < 0.05 ? ["idle", false] : [`${DELAYS[choice("dlTime")]} · ${fmtPct(val("dlMix"))}`, true]) },
  { key: "reverb", name: "REVERB", onId: "rvOn", what: "a vocal plate: a space for the voice to live",
    cells: () => [
      knob("rvDecay", "Decay", "tail length", (v) => `${v.toFixed(1)} s`, 1.6),
      knob("rvPredelay", "Pre-delay", "keeps words clear", fmtMs, 20),
      knob("rvMix", "Mix", "space level", fmtPct, 0),
      knob("rvTone", "Tone", "tail brightness", fmtHz, 7000),
      knob("rvDuck", "Duck", "blooms in gaps", fmtPct, 30),
    ],
    stat: () => (val("rvMix") < 0.05 ? ["idle", false] : [`${val("rvDecay").toFixed(1)} s · ${fmtPct(val("rvMix"))}`, true]) },
  { key: "out", name: "OUTPUT", onId: null, what: "final level into your mix",
    cells: () => [
      knob("outGain", "Output", "level", fmtSigned, 0, true),
      meter("Out Peak", "loudest sample", () => M.outPeak, 60, true, (v) => (v <= -99 ? "−inf" : fmtDb(v)), -60),
    ],
    stat: () => [fmtSigned(val("outGain")), Math.abs(val("outGain")) >= 0.05] },
];
let selected = 0;

// ---------------------------------------------------------------------------------------------
// Controls
let anyEdited = () => {};
const ARC = 135;
function arcPath(a0, a1, r = 40) {
  const p = (a) => [50 + r * Math.sin((a * Math.PI) / 180), 50 - r * Math.cos((a * Math.PI) / 180)];
  const [x0, y0] = p(a0), [x1, y1] = p(a1);
  return `M ${x0} ${y0} A ${r} ${r} 0 ${Math.abs(a1 - a0) > 180 ? 1 : 0} ${a1 > a0 ? 1 : 0} ${x1} ${y1}`;
}
let gradId = 0;
// The knob faces are the user's artwork (assets/knob_*.webp). The art stays still (so the gold drips
// never spin); a glowing value arc runs round the lattice ring and a pointer sits on the marble disc.
// Geometry in image pixels: inner disc centre / radius and outer ring radius (measured from the art).
const FACES = {
  db: { w: 226, h: 269, cx: 103, cy: 111, R: 68, outer: 106 },
  hz: { w: 223, h: 302, cx: 107, cy: 109, R: 65, outer: 104 },
  blank: { w: 223, h: 287, cx: 111, cy: 109, R: 66, outer: 105 },
};
function faceFor(id) {
  if (id === "ptSpeed") return ["blank", "ms"];
  if (/Freq|LowCut|Tone/.test(id)) return ["hz", ""];
  if (id === "rvDecay") return ["blank", "s"];
  if (id === "rvPredelay") return ["blank", "ms"];
  if (id === "cpRatio") return ["blank", ":1"];
  if (/Gain|Thr|Target|Range|Peak|Makeup|Drive|Cut/.test(id)) return ["db", ""];
  return ["blank", "%"];
}
function knobSvg(face, glyph) {
  const f = FACES[face], g = `kg${gradId++}`, r = (f.R + f.outer) / 2;
  const p = (a, rad) => [f.cx + rad * Math.sin((a * Math.PI) / 180), f.cy - rad * Math.cos((a * Math.PI) / 180)];
  const arc = (a0, a1) => { const [x0, y0] = p(a0, r), [x1, y1] = p(a1, r); return `M ${x0} ${y0} A ${r} ${r} 0 ${Math.abs(a1 - a0) > 180 ? 1 : 0} 1 ${x1} ${y1}`; };
  return `<img src="assets/knob_${face}.webp" alt="" draggable="false">
  <svg viewBox="0 0 ${f.w} ${f.h}">
    <defs><filter id="${g}g" x="-30%" y="-30%" width="160%" height="160%"><feGaussianBlur stdDeviation="3.5" result="b"/><feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter>
    <linearGradient id="${g}t" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#fff6cf"/><stop offset="0.5" stop-color="#e2b453"/><stop offset="1" stop-color="#9a6d1f"/></linearGradient></defs>
    <path d="${arc(-ARC, ARC)}" fill="none" stroke="rgba(20,40,70,0.22)" stroke-width="11" stroke-linecap="round"/>
    <path class="k-val" d="" fill="none" stroke="#7cc4ff" stroke-width="8" stroke-linecap="round" filter="url(#${g}g)"/>
    ${glyph ? `<text x="${f.cx}" y="${f.cy + 16}" text-anchor="middle" font-family="Georgia, 'Times New Roman', serif" font-size="${glyph.length > 1 ? 46 : 54}"
      fill="rgba(235,225,195,0.22)" stroke="#5e420f" stroke-width="7" stroke-linejoin="round">${glyph}</text>
    <text x="${f.cx}" y="${f.cy + 16}" text-anchor="middle" font-family="Georgia, 'Times New Roman', serif" font-size="${glyph.length > 1 ? 46 : 54}"
      fill="rgba(235,225,195,0.22)" stroke="url(#${g}t)" stroke-width="3.6" stroke-linejoin="round">${glyph}</text>` : ""}
    <g class="k-ptr"><rect x="${f.cx - 5}" y="${f.cy - f.R + 2}" width="10" height="22" rx="4" fill="#fffaf0" stroke="#8d641f" stroke-width="2.5"/></g>
  </svg>`;
}
const arcFor = (face, a0, a1) => {
  const f = FACES[face], r = (f.R + f.outer) / 2;
  const p = (a) => [f.cx + r * Math.sin((a * Math.PI) / 180), f.cy - r * Math.cos((a * Math.PI) / 180)];
  const [x0, y0] = p(a0), [x1, y1] = p(a1);
  return `M ${x0} ${y0} A ${r} ${r} 0 ${Math.abs(a1 - a0) > 180 ? 1 : 0} 1 ${x1} ${y1}`;
};

function knob(id, label, sub, fmt, def, bipolar = false) {
  const s = P[id];
  const [face, glyph] = faceFor(id);
  const fc = FACES[face];
  const cell = document.createElement("div");
  cell.className = "cell";
  cell.innerHTML = `<span class="cell-label">${label}</span><span class="cell-sub">${sub}</span>
    <div class="knob" tabindex="0" role="slider" aria-label="${label}">${knobSvg(face, glyph)}</div><span class="cell-value"></span>`;
  const k = cell.querySelector(".knob"), valEl = cell.querySelector(".cell-value");
  const arc = cell.querySelector(".k-val"), ptr = cell.querySelector(".k-ptr");
  const refresh = () => {
    const raw = s.getNormalisedValue();
    const n = Number.isFinite(raw) ? raw : 0;   // before the plug-in has sent the ranges
    const a = -ARC + 2 * ARC * clamp(n, 0, 1);
    const from = bipolar ? 0 : -ARC;
    arc.setAttribute("d", Math.abs(a - from) < 0.5 ? "" : arcFor(face, Math.min(from, a), Math.max(from, a)));
    ptr.setAttribute("transform", `rotate(${a} ${fc.cx} ${fc.cy})`);
    valEl.textContent = fmt(s.getScaledValue());
  };
  const setNorm = (n) => { s.setNormalisedValue(clamp(n, 0, 1)); refresh(); anyEdited(); };
  let drag = null;
  k.addEventListener("pointerdown", (e) => { k.setPointerCapture(e.pointerId); drag = { y: e.clientY, n: s.getNormalisedValue() }; s.sliderDragStarted(); e.preventDefault(); });
  k.addEventListener("pointermove", (e) => {
    if (!drag) return;
    const scale = stage.getBoundingClientRect().height / 900;
    setNorm(drag.n + (drag.y - e.clientY) / scale / (e.shiftKey ? 1200 : 220));
  });
  const end = () => { if (drag) { drag = null; s.sliderDragEnded(); } };
  k.addEventListener("pointerup", end);
  k.addEventListener("pointercancel", end);
  k.addEventListener("dblclick", () => { s.sliderDragStarted(); setNorm(scaledToNorm(s, def)); s.sliderDragEnded(); });
  k.addEventListener("wheel", (e) => { e.preventDefault(); s.sliderDragStarted(); setNorm(s.getNormalisedValue() + (e.deltaY < 0 ? 1 : -1) * (e.shiftKey ? 0.002 : 0.01)); s.sliderDragEnded(); }, { passive: false });
  k.addEventListener("keydown", (e) => {
    const step = e.shiftKey ? 0.002 : 0.01;
    if (e.key === "ArrowUp" || e.key === "ArrowRight") setNorm(s.getNormalisedValue() + step);
    else if (e.key === "ArrowDown" || e.key === "ArrowLeft") setNorm(s.getNormalisedValue() - step);
    else return;
    e.preventDefault();
  });
  s.valueChangedEvent.addListener(refresh);
  s.propertiesChangedEvent.addListener(refresh);
  refresh();
  cell.refresh = refresh;
  return cell;
}

function seg(id, label, sub, labels, vertical = false) {
  const s = P[id];
  const cell = document.createElement("div");
  cell.className = "cell";
  cell.innerHTML = `<span class="cell-label">${label}</span><span class="cell-sub">${sub}</span><div class="${vertical ? "vseg" : "seg"}"></div>`;
  const box = cell.querySelector(vertical ? ".vseg" : ".seg");
  const btns = labels.map((t, i) => {
    const b = document.createElement("button");
    b.type = "button"; b.textContent = t;
    b.addEventListener("click", () => { s.setChoiceIndex(i); refresh(); anyEdited(); });
    box.appendChild(b);
    return b;
  });
  const refresh = () => btns.forEach((b, i) => b.classList.toggle("sel", i === s.getChoiceIndex()));
  s.valueChangedEvent.addListener(refresh);
  s.propertiesChangedEvent.addListener(refresh);
  refresh();
  cell.refresh = refresh;
  return cell;
}
const vseg = (id, label, sub, labels) => seg(id, label, sub, labels, true);
/** A grid of choice buttons with a set number of columns (Key: 4 x 3, Scale: one column). */
function grid(id, label, sub, labels, cols) {
  const cell = seg(id, label, sub, labels, true);
  const box = cell.querySelector(".vseg");
  box.style.gridTemplateColumns = `repeat(${cols}, 1fr)`;
  box.classList.add(cols === 1 ? "list" : "keys");
  return cell;
}

function toggle(id, label, sub, onText, offText) {
  const s = P[id];
  const cell = document.createElement("div");
  cell.className = "cell";
  cell.innerHTML = `<span class="cell-label">${label}</span><span class="cell-sub">${sub}</span><button type="button" class="chip"></button>`;
  const b = cell.querySelector("button");
  const refresh = () => { b.textContent = s.getValue() ? onText : offText; b.classList.toggle("on", !!s.getValue()); };
  b.addEventListener("click", () => { s.setValue(!s.getValue()); refresh(); anyEdited(); });
  s.valueChangedEvent.addListener(refresh);
  refresh();
  cell.refresh = refresh;
  return cell;
}

const liveMeters = [];
function meter(label, sub, get, range, up, fmt, floor = 0) {
  const cell = document.createElement("div");
  cell.className = "cell meter-cell";
  cell.innerHTML = `<span class="cell-label">${label}</span><span class="cell-sub">${sub}</span><div class="mbar${up ? " up" : ""}"><i></i></div><span class="cell-value"></span>`;
  const bar = cell.querySelector("i"), v = cell.querySelector(".cell-value");
  cell.update = () => {
    const x = get();
    const frac = up === true ? (x - floor) / range : up === null ? Math.abs(x) / range : -x / range;
    bar.style.height = `${clamp(frac, 0, 1) * (bar.parentElement.clientHeight - 6)}px`;
    v.textContent = fmt(x);
  };
  liveMeters.push(cell);
  cell.update();
  return cell;
}

/** Dynamic EQ: one live bar per band (how much it's cutting right now). */
function dynMeter() {
  const cell = document.createElement("div");
  cell.className = "cell meter-cell dyn-meter";
  cell.innerHTML = `<span class="cell-label">Cutting</span><span class="cell-sub">right now</span><div class="dyn-bars">${DYN_BANDS.map((b) =>
    `<div class="dyn-col"><div class="mbar"><i></i></div><span class="dyn-name mono">${b.short}</span></div>`).join("")}</div><span class="cell-value"></span>`;
  const bars = [...cell.querySelectorAll(".mbar i")], cols = [...cell.querySelectorAll(".dyn-col")], v = cell.querySelector(".cell-value");
  cell.update = () => {
    let deepest = 0;
    bars.forEach((bar, b) => {
      const x = dynLive(b);
      deepest = Math.min(deepest, x);
      bar.style.height = `${clamp(-x / 12, 0, 1) * (bar.parentElement.clientHeight - 6)}px`;
      cols[b].classList.toggle("off", dynCut(b) < 0.05);
    });
    v.textContent = deepest > -0.1 ? "0 dB" : fmtDb(deepest);
  };
  liveMeters.push(cell);
  cell.update();
  return cell;
}

// Build every module's cells once (they stay bound to their parameters).
const cellsByModule = MODULES.map((m) => m.cells());

// ---------------------------------------------------------------------------------------------
// Honeycomb chain
const hive = $("hive");
const hexes = MODULES.map((m, i) => {
  const row = Math.floor(i / 2), col = i % 2;   // 12 cells in a honeycomb, two per row
  const h = document.createElement("div");
  h.className = "hex";
  h.style.setProperty("--tex", `linear-gradient(160deg, rgba(255,255,255,0.5), rgba(255,255,255,0) 45%), url("assets/${["marble_blue", "marble_cream", "marble_blue2", "marble_cream2"][(i * 3 + row) % 4]}.webp") center / cover`);
  h.style.left = `${col * 110 + (row % 2) * 55 + 22}px`;
  h.style.top = `${row * 92 + 2}px`;
  h.innerHTML = `<div class="rim"></div><div class="face"><span class="num">${String(i + 1).padStart(2, "0")}</span>
    <span class="name">${m.name}</span><span class="stat"></span>${m.onId ? '<button type="button" class="dot" aria-label="On / off"></button>' : ""}</div>`;
  h.addEventListener("click", () => select(i));
  const dot = h.querySelector(".dot");
  if (dot) dot.addEventListener("click", (e) => { e.stopPropagation(); P[m.onId].setValue(!on(m.onId)); refreshHive(); renderModuleHead(); anyEdited(); });
  hive.appendChild(h);
  return h;
});
function refreshHive() {
  MODULES.forEach((m, i) => {
    const h = hexes[i];
    const isOn = !m.onId || on(m.onId);
    const [text, active] = m.stat();
    h.classList.toggle("off", !isOn);
    h.classList.toggle("idle", !active);
    h.classList.toggle("sel", i === selected);
    h.querySelector(".stat").textContent = isOn ? text : "off";
  });
}

// ---------------------------------------------------------------------------------------------
// Module panel
const cellsEl = $("cells");
function renderModuleHead() {
  const m = MODULES[selected];
  $("mod-num").textContent = String(selected + 1).padStart(2, "0");
  $("mod-name").textContent = m.name;
  $("mod-what").textContent = m.what;
  const pw = $("mod-power");
  pw.hidden = !m.onId;
  if (m.onId) { pw.textContent = on(m.onId) ? "ON" : "OFF"; pw.classList.toggle("on", on(m.onId)); }
  $("mod-kept").hidden = !(report && report.ok && report.kept && report.kept[selected]);
}
$("mod-power").addEventListener("click", () => {
  const m = MODULES[selected];
  if (!m.onId) return;
  P[m.onId].setValue(!on(m.onId));
  renderModuleHead(); refreshHive(); anyEdited();
});
function select(i) {
  selected = i;
  cellsEl.dataset.cols = MODULES[i].key === "eq" || MODULES[i].key === "dyneq" ? "5" : "6";   // EQs: amounts on top, frequencies below
  cellsEl.dataset.page = MODULES[i].key;
  cellsEl.replaceChildren(...cellsByModule[i]);
  cellsByModule[i].forEach((c) => c.refresh && c.refresh());
  renderModuleHead();
  refreshHive();
  learnTab = "module";
  renderLearn();
}

// ---------------------------------------------------------------------------------------------
// Spectrum + Tone EQ curve
const canvas = $("spec"), ctx = canvas.getContext("2d");
const plot = $("plot");
const PW = 714, PH = 204;
canvas.width = PW * 2; canvas.height = PH * 2;
ctx.scale(2, 2);
const F_LO = 20, F_HI = 20000, DB_TOP = -6, DB_BOT = -84, EQ_VIEW = 15;
const xFor = (f) => (Math.log(f / F_LO) / Math.log(F_HI / F_LO)) * PW;
const fFor = (x) => F_LO * Math.pow(F_HI / F_LO, x / PW);
const ySpec = (db) => ((DB_TOP - db) / (DB_TOP - DB_BOT)) * PH;
const yEq = (db) => PH / 2 - (db / EQ_VIEW) * (PH / 2 - 14);
const dbForY = (y) => ((PH / 2 - y) / (PH / 2 - 14)) * EQ_VIEW;

function coeffs(type, f, gain, q, sr) {
  const w0 = (2 * Math.PI * Math.min(f, 0.45 * sr)) / sr, cw = Math.cos(w0), A = Math.pow(10, gain / 40), alpha = Math.sin(w0) / (2 * q);
  if (type === 1) {
    const a0 = 1 + alpha / A;
    return [(1 + alpha * A) / a0, (-2 * cw) / a0, (1 - alpha * A) / a0, (-2 * cw) / a0, (1 - alpha / A) / a0];
  }
  const k = 2 * Math.sqrt(A) * alpha;
  if (type === 0) {
    const a0 = A + 1 + (A - 1) * cw + k;
    return [(A * (A + 1 - (A - 1) * cw + k)) / a0, (2 * A * (A - 1 - (A + 1) * cw)) / a0, (A * (A + 1 - (A - 1) * cw - k)) / a0, (-2 * (A - 1 + (A + 1) * cw)) / a0, (A + 1 + (A - 1) * cw - k) / a0];
  }
  const a0 = A + 1 - (A - 1) * cw + k;
  return [(A * (A + 1 + (A - 1) * cw + k)) / a0, (-2 * A * (A - 1 + (A + 1) * cw)) / a0, (A * (A + 1 + (A - 1) * cw - k)) / a0, (2 * (A - 1 - (A + 1) * cw)) / a0, (A + 1 - (A - 1) * cw - k) / a0];
}
function magDb(c, f, sr) {
  const w = (2 * Math.PI * f) / sr, cw = Math.cos(w), c2 = Math.cos(2 * w);
  const [b0, b1, b2, a1, a2] = c;
  const num = b0 * b0 + b1 * b1 + b2 * b2 + 2 * (b0 * b1 + b1 * b2) * cw + 2 * b0 * b2 * c2;
  const den = 1 + a1 * a1 + a2 * a2 + 2 * (a1 + a1 * a2) * cw + 2 * a2 * c2;
  return 10 * Math.log10(Math.max(num, 1e-30) / Math.max(den, 1e-30));
}
function eqResponse(f) {
  if (!on("eqOn")) return 0;
  const sr = M.sr || 48000;
  let db = 0;
  EQ_BANDS.forEach((b, i) => { const g = eqGain(i); if (Math.abs(g) > 1e-4) db += magDb(coeffs(b.type, eqFreq(i), g, b.q, sr), f, sr); });
  return db;
}

/** Dynamic EQ curve for a set of cuts (dB, >= 0 each). */
function dynResponse(f, cuts) {
  const sr = M.sr || 48000;
  let db = 0;
  DYN_BANDS.forEach((b, i) => { if (cuts[i] > 1e-3) db += magDb(coeffs(1, dynFreq(i), -cuts[i], b.q, sr), f, sr); });
  return db;
}
const dynPage = () => MODULES[selected] && MODULES[selected].key === "dyneq";

const nodes = EQ_BANDS.map((b, i) => {
  const n = document.createElement("div");
  n.className = "node";
  n.textContent = String(i + 1);
  n.title = `${b.name}: drag to change, double-click to reset`;
  let drag = false;
  n.addEventListener("pointerdown", (e) => { n.setPointerCapture(e.pointerId); drag = true; P["eqGain" + (i + 1)].sliderDragStarted(); P["eqFreq" + (i + 1)].sliderDragStarted(); showNode(i); e.preventDefault(); });
  n.addEventListener("pointermove", (e) => {
    if (!drag) return;
    const r = plot.getBoundingClientRect(), scale = r.width / PW;
    const x = (e.clientX - r.left) / scale, y = (e.clientY - r.top) / scale;
    setScaled("eqFreq" + (i + 1), fFor(x));
    setScaled("eqGain" + (i + 1), clamp(dbForY(y), -12, 12));
    if (!on("eqOn")) P.eqOn.setValue(true);
    showNode(i); anyEdited();
  });
  const end = () => { if (drag) { drag = false; P["eqGain" + (i + 1)].sliderDragEnded(); P["eqFreq" + (i + 1)].sliderDragEnded(); $("eq-readout").textContent = ""; } };
  n.addEventListener("pointerup", end);
  n.addEventListener("pointercancel", end);
  n.addEventListener("dblclick", () => { P["eqGain" + (i + 1)].sliderDragStarted(); setScaled("eqGain" + (i + 1), 0); P["eqGain" + (i + 1)].sliderDragEnded(); anyEdited(); });
  $("eq-nodes").appendChild(n);
  return n;
});
// Dynamic EQ points: left / right = frequency, down = Max Cut (shown while its page is open).
const dynNodes = DYN_BANDS.map((b, i) => {
  const n = document.createElement("div");
  n.className = "node dyn";
  n.textContent = b.short;
  n.title = `${b.name}: drag to set where and how much it may cut, double-click to turn the band off`;
  const ids = ["dqCut" + (i + 1), "dqFreq" + (i + 1)];
  let drag = false;
  n.addEventListener("pointerdown", (e) => { n.setPointerCapture(e.pointerId); drag = true; ids.forEach((id) => P[id].sliderDragStarted()); showDynNode(i); e.preventDefault(); });
  n.addEventListener("pointermove", (e) => {
    if (!drag) return;
    const r = plot.getBoundingClientRect(), scale = r.width / PW;
    const x = (e.clientX - r.left) / scale, y = (e.clientY - r.top) / scale;
    setScaled("dqFreq" + (i + 1), fFor(x));
    setScaled("dqCut" + (i + 1), clamp(-dbForY(y), 0, 12));
    if (!on("dqOn")) P.dqOn.setValue(true);
    showDynNode(i); anyEdited();
  });
  const end = () => { if (drag) { drag = false; ids.forEach((id) => P[id].sliderDragEnded()); $("eq-readout").textContent = ""; } };
  n.addEventListener("pointerup", end);
  n.addEventListener("pointercancel", end);
  n.addEventListener("dblclick", () => { P[ids[0]].sliderDragStarted(); setScaled(ids[0], 0); P[ids[0]].sliderDragEnded(); anyEdited(); });
  $("eq-nodes").appendChild(n);
  return n;
});
function showDynNode(i) { $("eq-readout").textContent = `${DYN_BANDS[i].name}  up to ${fmtCut(dynCut(i))} at ${fmtHz(dynFreq(i))}`; }

function showNode(i) { $("eq-readout").textContent = `${EQ_BANDS[i].name}  ${fmtSigned(eqGain(i))} at ${fmtHz(eqFreq(i))}`; }

function drawSpectrum(data, color, fill) {
  if (!data || !data.length) return;
  ctx.beginPath();
  data.forEach((db, i) => {
    const f = 20 * Math.pow(1000, i / (data.length - 1));
    const x = xFor(f), y = ySpec(db);
    i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
  });
  if (fill) {
    ctx.lineTo(PW, PH); ctx.lineTo(0, PH); ctx.closePath();
    ctx.fillStyle = fill; ctx.fill();
  } else {
    ctx.strokeStyle = color; ctx.lineWidth = 2; ctx.stroke();
  }
}
function draw() {
  ctx.clearRect(0, 0, PW, PH);
  // grid
  ctx.lineWidth = 1;
  ctx.font = "11px Plex, monospace";
  for (const f of [50, 100, 200, 500, 1000, 2000, 5000, 10000]) {
    const x = Math.round(xFor(f)) + 0.5;
    ctx.strokeStyle = "rgba(141,100,31,0.16)"; ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, PH); ctx.stroke();
    ctx.fillStyle = "rgba(86,97,112,0.8)"; ctx.fillText(f >= 1000 ? `${f / 1000}k` : `${f}`, x + 3, PH - 5);
  }
  ctx.strokeStyle = "rgba(36,97,143,0.25)"; ctx.beginPath(); ctx.moveTo(0, yEq(0) + 0.5); ctx.lineTo(PW, yEq(0) + 0.5); ctx.stroke();
  drawSpectrum(M.in, null, "rgba(157,182,201,0.45)");
  drawSpectrum(M.out, "#c9973a", null);
  // Dynamic EQ: the most each band may cut (dashed, on its page) and what it cuts right now (filled).
  const dynOn = on("dqOn") && !on("bypass");
  const page = dynPage();
  if (page) {
    const maxCuts = [0, 1, 2, 3].map((b) => (on("dqOn") ? dynCut(b) : 0));
    if (maxCuts.some((c) => c >= 0.05)) {
      ctx.beginPath();
      for (let x = 0; x <= PW; x += 3) { const y = yEq(dynResponse(fFor(x), maxCuts)); x ? ctx.lineTo(x, y) : ctx.moveTo(x, y); }
      ctx.setLineDash([6, 5]); ctx.strokeStyle = "rgba(141,100,31,0.75)"; ctx.lineWidth = 1.8; ctx.stroke(); ctx.setLineDash([]);
    }
  }
  const live = [0, 1, 2, 3].map((b) => (dynOn ? -dynLive(b) : 0));
  if (live.some((c) => c >= 0.1)) {
    ctx.beginPath();
    ctx.moveTo(0, yEq(0));
    for (let x = 0; x <= PW; x += 3) ctx.lineTo(x, yEq(dynResponse(fFor(x), live)));
    ctx.lineTo(PW, yEq(0)); ctx.closePath();
    ctx.fillStyle = "rgba(214,120,60,0.30)"; ctx.fill();
    ctx.strokeStyle = "#c8642c"; ctx.lineWidth = 2; ctx.stroke();
  }
  // Tone EQ curve
  ctx.beginPath();
  for (let x = 0; x <= PW; x += 3) { const y = yEq(eqResponse(fFor(x))); x ? ctx.lineTo(x, y) : ctx.moveTo(x, y); }
  ctx.strokeStyle = on("eqOn") ? (page ? "rgba(36,97,143,0.45)" : "#24618f") : "rgba(36,97,143,0.35)"; ctx.lineWidth = 2.5; ctx.stroke();
  nodes.forEach((n, i) => {
    n.hidden = page;
    n.style.left = `${(xFor(eqFreq(i)) / PW) * 100}%`;
    n.style.top = `${(yEq(eqGain(i)) / PH) * 100}%`;
    n.classList.toggle("flat", Math.abs(eqGain(i)) < 0.05);
  });
  dynNodes.forEach((n, i) => {
    n.hidden = !page;
    n.style.left = `${(xFor(dynFreq(i)) / PW) * 100}%`;
    n.style.top = `${(yEq(-dynCut(i)) / PH) * 100}%`;
    n.classList.toggle("flat", dynCut(i) < 0.05);
  });
}

// ---------------------------------------------------------------------------------------------
// Learn
const body = $("learn-body");
const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text !== undefined) e.textContent = text; return e; };
const head = (t) => el("span", "l-h", t);
const para = (t, cls = "l-p") => el("p", cls, t);
function item(name, value, why) {
  const d = el("div", "l-item");
  const top = el("div", "l-top");
  top.append(el("span", "", name), el("span", "l-val", value));
  d.append(top);
  if (why) d.append(el("div", "l-why", why));
  return d;
}
/** A tip with a plan: kind "warn" (do something), "calm" (fine as it is) or "" (info). */
function tip(title, text, kind, plan) {
  const d = el("div", `tip ${kind || ""}`);
  d.append(el("div", "t-title", title), el("div", "", text));
  if (plan) {
    d.append(el("div", "t-need", `Do I need to fix it? ${plan.need}`));
    if (plan.steps && plan.steps.length) {
      const ol = el("ol");
      plan.steps.forEach((s) => ol.append(el("li", "", s)));
      d.append(ol);
    }
  }
  return d;
}
function list(items) { const ul = el("ul", "l-list"); items.forEach((t) => ul.append(el("li", "", t))); return ul; }

const LEARN = {
  pitch: {
    does: "Pitch correction (auto-tune). It hears the note you sing, picks the nearest note of your key, and pulls you onto it. Your voice's tone stays the same (no chipmunk sound); breaths and s sounds are never touched. It's first in the chain, so everything after it hears the tuned voice.",
    how: ["Key / Scale: set them to your beat's key (often in the beat's name, e.g. \"A min\"). Chromatic allows all 12 notes when you're not sure.",
      "Retune: 0 - 10 ms gives the hard, robotic trap sound; 30 - 80 ms sounds tuned but natural; 100+ ms only fixes drift.",
      "Humanize: lets long held notes keep their vibrato while short notes still snap in.",
      "Amount: 100 % lands right on the note; lower keeps some of your own pitch.",
      "Already using Auto-Tune or Melodyne? Turn this off: one tuner is enough."],
    live: () => {
      const t = [];
      if (val("ptAmount") >= 0.05 && choice("ptScale") === 0) t.push(tip("CHROMATIC", "All 12 notes are allowed, so a wrong note can't be pulled into the key; it just gets cleaned up.", "calm",
        { need: "Optional. It works; the right key sounds tighter.", steps: ["Set Key and Scale to your beat's key (Auto-Edit suggests one in its report)."] }));
      if (val("ptAmount") >= 0.05 && M.pitchTarget >= 0 && Math.abs(M.pitchCorr) > 0.9) t.push(tip("BIG JUMP", `It's moving your voice ${Math.abs(M.pitchCorr * 100).toFixed(0)} cents right now. Big moves can sound warbly on held notes.`, "calm",
        { need: "Only if it sounds off. Big moves usually mean the Key / Scale doesn't match the beat.", steps: ["Check the Key / Scale match your beat.", "Or raise Retune to 40 ms or more for a softer pull."] }));
      return t;
    } },
  cleanup: {
    does: "Two clean-up tools before anything else. Low Cut removes everything under the voice: rumble, AC hum, mic-stand bumps and the boom of p and b pops. The gate turns the gaps between your phrases down, so room noise and headphone bleed don't get louder when the compressor works.",
    how: ["Low Cut: raise it until the voice starts to thin, then back off 10 - 20 Hz. Deep male voices sit around 70 - 90 Hz, higher voices 100 - 150 Hz.",
      "Gate Threshold: set it between the noise in your gaps and your quietest words.",
      "Gate Range: how far the gaps go down. 10 - 15 dB sounds natural; more can sound choppy."],
    live: () => {
      const t = [];
      if (val("clGateRange") >= 20) t.push(tip("DEEP GATE", `The gaps go down ${fmtNum(val("clGateRange"), 0)} dB. Breaths vanish completely, which can sound robotic.`, "calm",
        { need: "Optional. Fine if you like it dead-quiet between lines.", steps: ["Lower Gate Range to about 12 dB for a more natural sound."] }));
      return t;
    } },
  eq: {
    does: "Shapes the tone of your voice in five musical spots. Body is the weight and chest. Mud is the boxy, cloudy build-up most home recordings have. Nasal is the honky, phone-like zone. Presence is where the words live. Air is the breathy sheen on top.",
    how: ["Drag the numbered points on the spectrum, or use the knobs. Double-click resets a band.",
      "Cut narrow, boost wide: small cuts in Mud and Nasal often help more than big boosts.",
      "Boosting Presence and Air also makes s sounds sharper; the De-Esser comes right after to catch that."],
    live: () => {
      const t = [];
      const big = [0, 1, 2, 3, 4].filter((b) => Math.abs(eqGain(b)) > 8);
      if (big.length) t.push(tip("BIG EQ MOVE", `${big.map((b) => EQ_BANDS[b].name).join(", ")} ${big.length > 1 ? "are" : "is"} moved more than 8 dB. That usually means the recording itself needs a fix.`, "calm",
        { need: "Optional. Use your ears: if it sounds good, it is good.", steps: ["Try halving the move and compare with A / B.", "Next take: check mic distance (a fist away) and room echo."] }));
      return t;
    } },
  dyneq: {
    does: "An EQ that only works when it's needed. Some words boom, some vowels go muddy or honky, some loud notes get piercing, but the rest of the time your voice is fine. Each of the four bands learns how that part of your voice normally sits and cuts it only in the moments it jumps out, then lets go. The steady tone stays Tone EQ's job, and s sounds stay the De-Esser's.",
    how: ["Max Cut (top row): the most a band may cut. Off = band not used. 2 - 4 dB is natural; 6 dB+ is strong.",
      "Frequency (bottom row): where the problem lives. Easy trick: raise Max Cut, then sweep Frequency while a problem word plays until the orange cut appears on it.",
      "Sensitivity: how far a band may rise above your normal before it's pulled back. Higher catches more.",
      "On the spectrum, drag the B, M, N, H points: left / right = where, down = Max Cut. The orange shape is what it's cutting right now."],
    live: () => {
      const t = [];
      if (on("dqOn") && [0, 1, 2, 3].every((b) => dynCut(b) < 0.05)) t.push(tip("NO BANDS SET", "Every band's Max Cut is Off, so the Dynamic EQ does nothing yet.", "calm",
        { need: "No. Only use it if some words boom, cloud up, honk or bite.", steps: ["Run Auto-Edit: it finds the spots that jump out in your voice.", "Or raise a band's Max Cut to 3 dB and sweep its Frequency while the problem word plays."] }));
      const maxed = [0, 1, 2, 3].filter((b) => dynCut(b) >= 1 && dynLive(b) <= -(dynCut(b) - 0.15));
      if (maxed.length) t.push(tip("AT THE LIMIT", `${maxed.map((b) => DYN_BANDS[b].name).join(", ")} ${maxed.length > 1 ? "are" : "is"} cutting the full Max Cut right now: that spot jumps out further than the band is allowed to fix.`, "calm",
        { need: "Optional. Fine if it sounds right.", steps: ["Raise that band's Max Cut 1 - 2 dB and listen on the problem word.", "If it's cutting nearly all the time, the spot is too strong overall: cut it in Tone EQ instead."] }));
      return t;
    } },
  deess: {
    does: "Turns harsh s, t, sh and ch sounds down only while they happen. It compares the sizzle band with the rest of your voice, so it works the same whether you whisper or shout, and it only touches the top end, never the body of the word.",
    how: ["Amount: how hard it cuts. Raise it until s sounds stop stinging on earbuds.",
      "Sensitivity: how easily a sound counts as an s. Raise it if some s sounds slip through.",
      "Frequency: where the cut starts. Lower for a lispy, thick s; higher for a thin whistle."],
    live: () => {
      const t = [];
      if (M.deEss < -9) t.push(tip("CUTTING HARD", `It's cutting ${fmtDb(M.deEss)} on the loudest s sounds. Too much makes the vocal sound lispy, like "th" instead of "s".`, "warn",
        { need: "Only if you hear a lisp.", steps: ["Lower Amount by 10 % at a time while listening to an s-heavy line.", "Or lower the Air band in Tone EQ by 1 - 2 dB."] }));
      return t;
    } },
  rider: {
    does: "Rides the vocal level like an engineer on a fader: quiet words come up, loud ones come down, toward Target. That way the compressor has less to do and the vocal stays natural. It holds still in the gaps, so it never pumps up noise or breaths.",
    how: ["Target: the level it steers toward. Auto-Edit sets it to the middle of your performance.",
      "Range: the most it may move either way. 3 - 6 dB is plenty for most takes.",
      "Speed: Fast for rap (word by word), Slow for sung notes (phrase by phrase)."],
    live: () => [] },
  comp: {
    does: "Two compressors in one, like a pro vocal chain. Peak catches only the sudden loud syllables (fast). The leveler (Threshold + Ratio) smooths the whole performance so it sits at one steady level on top of the beat. Makeup brings the level back up, Mix blends in the uncompressed vocal (parallel compression).",
    how: ["Peak: lower it until the loudest words get about 3 - 6 dB of reduction.",
      "Threshold: lower it until the meter shows about 3 - 6 dB while you sing.",
      "Ratio: 2 - 3:1 is gentle, 4:1 is a modern rap vocal, 6:1+ is very controlled.",
      "Mix below 100 % keeps some life if it sounds squashed."],
    live: () => {
      const t = [];
      const gr = M.peakGr + M.levelGr;
      if (gr < -14) t.push(tip("SQUASHED", `About ${fmtDb(gr)} of reduction right now. That much flattens the performance and brings up breaths and room noise.`, "warn",
        { need: "Only if it sounds flat or breathy to you.", steps: ["Raise Threshold 2 - 3 dB.", "Or lower Mix to 70 - 80 % to bring some life back."] }));
      return t;
    } },
  sat: {
    does: "Adds harmonics: the warmth, density and edge that make a vocal sound finished and help it come through on phone speakers. Tape is smooth glue, Tube is warm and rich, Clip is gritty and aggressive.",
    how: ["Drive: how hard it's pushed. Felt more than heard is the goal for a lead vocal.",
      "Mix: blend with the clean voice. 30 - 50 % keeps it natural.",
      "Turn Drive up until you hear it, then back off a little."],
    live: () => {
      const t = [];
      if (M.satHarm > -18 && M.satHarm > -99) t.push(tip("AUDIBLE DISTORTION", `Harmonics are at ${fmtDb(M.satHarm)}, loud enough to hear as distortion.`, "calm",
        { need: "Your choice: great as an effect (ad-libs, hooks), too much for most leads.", steps: ["Lower Drive 3 dB, or lower Mix to 30 %."] }));
      return t;
    } },
  double: {
    does: "Makes the vocal sound stacked: two slightly late, slowly drifting copies, one left and one right, like a second take. The lead stays in the centre.",
    how: ["Amount: how loud the copies are. 20 - 35 % for a lead, more for ad-libs and hooks.",
      "Width: how far apart. Lower it if the vocal sounds hollow in mono (phone speaker)."],
    live: () => [] },
  delay: {
    does: "Echoes locked to your song's tempo, so they groove with the beat. Duck turns the echoes down while you're rapping and lets them come up in the gaps, so words stay clear.",
    how: ["Time: 1/4 and 1/8 are classic; 1/8 dot gives a bouncy feel.",
      "Mix: keep it low (10 - 20 %) so you feel it more than hear it.",
      "Tone: lower makes the repeats darker so they sit behind the vocal."],
    live: () => (M.bpm > 0 ? [] : [tip("NO TEMPO YET", "The delay locks to Cubase's tempo once playback runs (120 BPM until then).", "")]) },
  reverb: {
    does: "Puts the vocal in a space, like a vocal plate in a studio. Pre-delay keeps the start of each word dry and clear; Duck lets the tail bloom in the gaps. The low end of the reverb is cut so it never muddies the 808.",
    how: ["Decay: 0.8 - 1.5 s for rap, 1.5 - 3 s for sung parts.",
      "Mix: 8 - 20 %. If you notice the reverb, it's probably a bit much.",
      "Pre-delay: 20 - 40 ms keeps words upfront."],
    live: () => {
      const t = [];
      if (val("rvMix") > 35) t.push(tip("WASHY", `Reverb Mix is ${fmtPct(val("rvMix"))}. That pushes the vocal back and blurs words.`, "calm",
        { need: "Optional: great for a dreamy effect.", steps: ["Lower Mix to 10 - 20 %, or raise Duck so the tail stays out of the words."] }));
      return t;
    } },
  out: {
    does: "The final level into your mix. Auto-Edit matches it to the loudness the vocal came in at, so it doesn't jump in your mix; set the vocal's balance against the beat with Cubase's track fader.",
    how: ["Use A / B with MATCH on to hear what Voxology does at equal loudness.",
      "Keep Out Peak below about -1 dB on the vocal track."],
    live: () => {
      const t = [];
      if (M.outPeak > -0.5) t.push(tip("CLIPPING RISK", `The output peaks at ${fmtDb(M.outPeak)}, right at the top.`, "warn",
        { need: "Yes, if the track meter in Cubase turns red.", steps: ["Lower Output 3 dB.", "Or lower Compressor Makeup."] }));
      return t;
    } },
};

function notesAsTips() {
  for (const raw of report.notes || []) {
    const lines = raw.split("\n");
    const first = lines[0], cut = first.indexOf(":");
    const title = cut > 0 && cut < 32 ? first.slice(0, cut) : "HEADS UP";
    const text = cut > 0 && cut < 32 ? first.slice(cut + 1).trim() : first;
    const need = (lines.find((l) => l.startsWith("NEED:")) || "").slice(5).trim();
    const steps = lines.filter((l) => l.startsWith("STEP:")).map((l) => l.slice(5).trim());
    const calm = /^(No|Optional)/i.test(need);
    body.append(tip(title, text.charAt(0).toUpperCase() + text.slice(1), calm ? "calm" : "warn", need ? { need, steps } : undefined));
  }
}

function renderReport() {
  if (!report) {
    body.append(head("AUTO-EDIT"), para("Auto-Edit listens to your vocal, sets every module for the style you pick and explains each choice here."));
    const ol = el("ol", "l-list");
    ["Pick a STYLE (Trap Lead, Rap, Melodic, Ad-libs, R&B) and an INTENSITY.", "Press AUTO-EDIT, then play a part of the song where you're rapping or singing.",
      "After about 12 seconds of voice it sets everything. Read why here, compare with A / B, and press UNDO if you don't like it."].forEach((s) => ol.append(el("li", "", s)));
    body.append(ol);
    return;
  }
  if (!report.ok) {
    body.append(head("AUTO-EDIT"), para(report.summary || ""));
    body.append(tip(report.tipTitle || "TRY AGAIN", report.tip || "Play a part where you're singing, then press Auto-Edit.", "warn"));
    return;
  }
  body.append(head("AUTO-EDIT REPORT"), para(`${report.style} · ${report.intensity}`, "l-meta"), para(report.summary));
  notesAsTips();
  MODULES.forEach((m, i) => {
    const rs = (report.reasons || []).filter((r) => r.module === m.key);
    if (!rs.length) return;
    body.append(el("div", "l-sep"));
    const h = head(`${String(i + 1).padStart(2, "0")} ${m.name}`);
    if (report.kept && report.kept[i]) h.append(el("span", "l-kept", "  · CHECKED · KEPT"));
    body.append(h);
    rs.forEach((r) => body.append(item(r.control, r.value, r.why)));
  });
  if (report.tips && report.tips.length) { body.append(el("div", "l-sep"), head("SUGGESTIONS"), list(report.tips)); }
}

function renderModuleLearn() {
  const m = MODULES[selected], L = LEARN[m.key];
  body.append(head(`${String(selected + 1).padStart(2, "0")} ${m.name}`), para(L.does));
  if (m.onId && !on(m.onId)) body.append(tip("SWITCHED OFF", "This module is off, so it doesn't change your vocal.", "calm", { need: "No.", steps: ["Click ON at the top of the module (or the dot on its cell) to use it."] }));
  L.live().forEach((t) => body.append(t));
  body.append(head("HOW TO USE IT"), list(L.how));
  if (report && report.ok) {
    const rs = (report.reasons || []).filter((r) => r.module === m.key);
    if (rs.length) {
      body.append(head(report.kept && report.kept[selected] ? "AUTO-EDIT LOOKED · KEPT" : "WHAT AUTO-EDIT DID HERE"));
      rs.forEach((r) => body.append(item(r.control, r.value, r.why)));
    }
  }
}

let learnKey = "";
function renderLearn(force = true) {
  // Live tips change with the meters: rebuild only when what's shown would change.
  const m = MODULES[selected];
  const key = learnTab + selected + (learnTab === "module" ? LEARN[m.key].live().map((t) => t.textContent).join("|") + (m.onId ? on(m.onId) : "") : "") + (report ? report.time : "");
  if (!force && key === learnKey) return;
  learnKey = key;
  const scroll = body.scrollTop;
  body.replaceChildren();
  document.querySelectorAll("#learn-tabs button").forEach((b) => b.classList.toggle("sel", b.dataset.tab === learnTab));
  if (learnTab === "report") renderReport(); else renderModuleLearn();
  if (!force) body.scrollTop = scroll;
}
document.querySelectorAll("#learn-tabs button").forEach((b) => b.addEventListener("click", () => { learnTab = b.dataset.tab; body.scrollTop = 0; renderLearn(); }));

// ---------------------------------------------------------------------------------------------
// Header
function refreshStyle() { $("style-name").textContent = STYLES[choice("aeStyle")] || STYLES[0]; }
$("style-prev").addEventListener("click", () => { P.aeStyle.setChoiceIndex((choice("aeStyle") + STYLES.length - 1) % STYLES.length); refreshStyle(); });
$("style-next").addEventListener("click", () => { P.aeStyle.setChoiceIndex((choice("aeStyle") + 1) % STYLES.length); refreshStyle(); });
P.aeStyle.valueChangedEvent.addListener(refreshStyle);
P.aeStyle.propertiesChangedEvent.addListener(refreshStyle);

const intensityBtns = INTENSITIES.map((t, i) => {
  const b = el("button", "", t.toUpperCase());
  b.type = "button";
  b.addEventListener("click", () => { P.aeIntensity.setChoiceIndex(i); refreshIntensity(); });
  $("intensity").append(b);
  return b;
});
function refreshIntensity() { intensityBtns.forEach((b, i) => b.classList.toggle("sel", i === choice("aeIntensity"))); }
P.aeIntensity.valueChangedEvent.addListener(refreshIntensity);
P.aeIntensity.propertiesChangedEvent.addListener(refreshIntensity);

const abBtns = document.querySelectorAll("#ab button");
function refreshAB() {
  abBtns.forEach((b) => b.classList.toggle("sel", (b.dataset.ab === "a") === on("listenA")));
  $("match").classList.toggle("on", on("levelMatch"));
  $("bypass").classList.toggle("off", on("bypass"));
}
abBtns.forEach((b) => b.addEventListener("click", () => { P.listenA.setValue(b.dataset.ab === "a"); refreshAB(); }));
$("match").addEventListener("click", () => { P.levelMatch.setValue(!on("levelMatch")); refreshAB(); });
$("bypass").addEventListener("click", () => { P.bypass.setValue(!on("bypass")); refreshAB(); });
for (const id of ["listenA", "levelMatch", "bypass"]) P[id].valueChangedEvent.addListener(refreshAB);

$("auto-edit").addEventListener("click", async () => {
  if (M.aeState === 1) { await aeCancel(); return; }
  if (M.aeState === 0) {
    const ok = await aeStart();
    if (ok) { M.aeState = 1; M.aeProgress = 0; refreshAutoEdit(); }
  }
});
$("undo").addEventListener("click", async () => { if (await aeUndo()) { report = null; learnTab = "report"; renderLearn(); refreshAll(); } });

function refreshAutoEdit() {
  const b = $("auto-edit"), text = $("ae-text"), sub = $("ae-sub");
  b.classList.toggle("busy", M.aeState !== 0);
  b.classList.toggle("pulse", M.aeState === 1 && !M.aeHearing);
  b.querySelector(".ae-fill").style.width = M.aeState === 1 ? `${Math.round(M.aeProgress * 100)}%` : M.aeState === 2 ? "100%" : "0";
  if (M.aeState === 1) {
    text.textContent = M.aeHearing ? `LISTENING ${Math.round(M.aeProgress * 100)} %` : "PRESS PLAY";
    sub.textContent = M.aeHearing ? "keep your vocal playing · click to cancel" : "play a part where you sing or rap";
  } else if (M.aeState === 2) { text.textContent = "THINKING…"; sub.textContent = "choosing every setting"; }
  else { text.textContent = "AUTO-EDIT"; sub.textContent = "listens, then sets everything"; }
  $("undo").disabled = !M.aeUndo;
}

let reportVersion = -1;
async function fetchReport(switchTab) {
  const json = await aeGetReport();
  try { report = json ? JSON.parse(json) : null; } catch { report = null; }
  if (switchTab && report) { learnTab = "report"; body.scrollTop = 0; }
  renderModuleHead();
  renderLearn();
}

// ---------------------------------------------------------------------------------------------
// Meters
function refreshAll() {
  cellsByModule.flat().forEach((c) => c.refresh && c.refresh());
  refreshHive(); renderModuleHead(); refreshAB(); refreshStyle(); refreshIntensity();
}
anyEdited = () => { refreshHive(); };

function meterBar(elBar, elText, short, peak) {
  elBar.style.width = `${clamp((peak + 60) / 60, 0, 1) * 100}%`;
  elText.textContent = `${short <= -99 ? "−inf" : fmtNum(short)} LUFS · ${peak <= -99 ? "−inf" : fmtNum(peak)}`;
}

window.__JUCE__.backend.addEventListener("voxMeters", (frame) => {
  Object.assign(M, typeof frame === "string" ? JSON.parse(frame) : frame);
  meterBar($("in-bar"), $("in-text"), M.inShort, M.inPeak);
  meterBar($("out-bar"), $("out-text"), M.outShort, M.outPeak);
  liveMeters.forEach((c) => c.update());
  refreshAutoEdit();
  refreshHive();
  if (M.aeReport !== reportVersion) { const first = reportVersion === -1; reportVersion = M.aeReport; fetchReport(!first); }
  const match = on("levelMatch") ? ` · MATCH ${fmtSigned(M.matchDb)}` : "";
  $("status").textContent = `${on("bypass") ? "BYPASSED" : on("listenA") ? "A: ORIGINAL" : "B: VOXOLOGY"}${match}${M.bpm > 0 ? ` · ${M.bpm.toFixed(0)} BPM` : ""}`;
  draw();
  if (learnTab === "module") renderLearn(false);
});

// Refresh everything once the parameter values have arrived.
for (const id of [...SLIDERS, ...TOGGLES, ...COMBOS]) P[id].valueChangedEvent.addListener(() => { refreshHive(); });
select(0);
setTimeout(refreshAll, 100);
draw();
