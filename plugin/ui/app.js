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
const fmtSt = (v) => (Math.abs(v) < 0.05 ? "0 st" : `${v > 0 ? "+" : MINUS}${Math.abs(v).toFixed(1)} st`);
const INTERVALS = ["Off", "3rd up", "5th up", "Octave up", "3rd down", "4th down", "5th down", "Octave down"];
const INTERVALS_SHORT = ["Off", "3rd \u2191", "5th \u2191", "8ve \u2191", "3rd \u2193", "4th \u2193", "5th \u2193", "8ve \u2193"];

// ---------------------------------------------------------------------------------------------
// Scale the fixed 1600x900 design to the window.
const stage = $("stage");
function fit() { stage.style.transform = `scale(${Math.min(window.innerWidth / 1600, window.innerHeight / 900)})`; }
window.addEventListener("resize", fit);
fit();

// ---------------------------------------------------------------------------------------------
// Parameters
const NOTES_C = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
const SLIDERS = ["ptAmount", "ptSpeed", "ptHumanize", "ptFormant", "ptVibrato", "ptTranspose", "hvLevel", "hvFormant", "clLowCut", "clGateThr", "clGateRange", "clPops", "clBreath", "dsAmount", "dsSens", "dsFreq", "rdTarget", "rdRange",
  "cpPeak", "cpThr", "cpRatio", "cpMakeup", "cpMix", "saDrive", "saMix", "dbAmount", "dbWidth",
  "dlFeedback", "dlMix", "dlTone", "dlDuck", "rvDecay", "rvPredelay", "rvMix", "rvTone", "rvDuck", "outGain", "umAmount",
  ...[1, 2, 3, 4, 5].flatMap((b) => ["eqGain" + b, "eqFreq" + b]), "dqSens", ...[1, 2, 3, 4].flatMap((b) => ["dqCut" + b, "dqFreq" + b])];
const PT_RM = NOTES_C.map((_, k) => "ptRm" + k);
const TOGGLES = [...PT_RM, "recMode", "bypass", "listenA", "levelMatch", "ptOn", "clOn", "eqOn", "dqOn", "dsOn", "rdOn", "cpOn", "saOn", "dbOn", "dlOn", "dlPing", "rvOn"];
const COMBOS = ["aeStyle", "aeIntensity", "ptKey", "ptScale", "ptMode", "ptKeySrc", "ptMidi", "rdSpeed", "saMode", "dlTime", "mode", "umFocus", "hv1", "hv2"];
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
const refChoose = Juce.getNativeFunction("chooseReference");
const refClear = Juce.getNativeFunction("clearReference");
const refGet = Juce.getNativeFunction("getReference");
const umGetSources = Juce.getNativeFunction("getUnmaskSources");
const umSetSource = Juce.getNativeFunction("setUnmaskSource");
let refInfo = { state: "none" };   // Reference Match: none / loading / ok / problem

function scaledToNorm(state, v) {
  const { start, end, skew } = state.properties;
  return Math.pow(clamp((v - start) / (end - start), 0, 1), skew);
}
function setScaled(id, v) { P[id].setNormalisedValue(scaledToNorm(P[id], v)); }

// Latest meter frame.
const M = { pitchSung: 0, pitchTarget: -1, pitchCorr: 0, inShort: -100, outShort: -100, inPeak: -100, outPeak: -100, gate: 0, pops: 0, breath: 0, clipNow: 0, clipTotal: 0, overNow: 0, hotPeak: -100, dyn: [0, 0, 0, 0], deEss: 0, rider: 0, peakGr: 0, levelGr: 0,
  satHarm: -100, matchDb: 0, bpm: 0, sr: 48000, aeState: 0, aeProgress: 0, aeHearing: false, aeUndo: false, aeReport: 0, refVersion: 0, umDip: [0, 0, 0, 0, 0, 0], umVocal: [-120, -120, -120, -120, -120, -120], umLink: 0, hvNotes: [-1, -1],
  bkState: 0, bkSource: 0, scState: 0, bkKey: 0, bkMode: 0, bkSet: 0, bkUnclear: 0, bkOpen: -1, bkConf: 0, bkTune: 0, bkHeard: 0, keyUsed: 0, scaleUsed: 0, midiNotes: 0, notesUsed: 0xFFF, recMode: 0, latencyMs: 33, in: null, out: null };
let report = null;          // last Auto-Edit report (parsed) or null
let learnTab = "module";

// ---------------------------------------------------------------------------------------------
// Modules
const STYLES = ["Trap Lead", "Rap", "Melodic", "Robot", "R&B", "Pop", "Folk", "Natural Singer"];
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
const SCALES = ["Chromatic", "Major", "Minor", "Harmonic Minor", "Minor Pentatonic", "Major Pentatonic", "Dorian", "Phrygian", "Mixolydian", "Blues"];
const SCALES_SHORT = ["Chromatic", "Major", "Minor", "Harm. Minor", "Minor Penta", "Major Penta", "Dorian", "Phrygian", "Mixolydian", "Blues"];
const PITCH_MODES = ["Natural", "Classic", "Robot"];
const fmtVib = (v) => (Math.abs(v) < 0.5 ? "as sung" : v <= -99.5 ? "flat" : `${v > 0 ? "+" : MINUS}${Math.abs(Math.round(v))} %`);

/** The beat's key: its home note and mode ("B minor", "E mixolydian"); short form "Bm", "E mix". */
const MODE_NAMES = { 0: "major", 2: "dorian", 4: "phrygian", 5: "lydian", 7: "mixolydian", 9: "minor", 11: "locrian" };
const MODE_SHORT = { 0: "", 2: " dor", 4: " phr", 5: " lyd", 7: " mix", 9: "m", 11: " loc" };
function beatKeyName(short = false) {
  return short ? `${NOTES[M.bkKey]}${MODE_SHORT[M.bkMode] ?? ""}` : `${NOTES[M.bkKey]} ${MODE_NAMES[M.bkMode] ?? "major"}`;
}
const fmtTune = (c) => `${c >= 0 ? "+" : MINUS}${Math.abs(c).toFixed(0)}\u00A2`;

/** Key cell: AUTO | MANUAL source switch, the status line and the 12 keys. Following a key (the beat's,
    or the voice's own), the keys show the one in use (gold); clicking a key switches to Manual. */
function keyCell() {
  const cell = grid("ptKey", "Key", "", NOTES, 4);
  cell.classList.add("pt-key");
  const sub = cell.querySelector(".cell-sub");
  const sw = document.createElement("div");
  sw.className = "key-src";
  const btns = ["Auto", "Manual"].map((t, i) => {
    const b = document.createElement("button");
    b.type = "button"; b.textContent = t;
    b.addEventListener("click", () => { P.ptKeySrc.setChoiceIndex(i); cell.update(); anyEdited(); });
    sw.appendChild(b);
    return b;
  });
  sub.after(sw);
  const keys = [...cell.querySelectorAll(".vseg button")];
  keys.forEach((b) => b.addEventListener("click", () => { if (choice("ptKeySrc") === 0 && M.bkState === 1) P.ptKeySrc.setChoiceIndex(1); }, true));
  cell.update = () => {
    const src = choice("ptKeySrc");
    btns.forEach((b, i) => b.classList.toggle("sel", i === src));
    const following = M.bkState === 1;
    cell.classList.toggle("following", following);
    keys.forEach((b, i) => b.classList.toggle("beat", following && i === M.keyUsed));
    const from = M.bkSource === 3 ? "voice" : "beat";
    const scSilent = M.scState === 1 && M.bkSource !== 2 && M.inShort > -70;   // side-chain on, vocal playing, beat silent
    sub.textContent = src === 1 ? "you set it" : scSilent && !following ? "beat: no sound"
      : following ? `${from}: ${beatKeyName(true)}`
      : M.bkState === 3 ? `${from}: listening\u2026` : "sing or play";
    sub.classList.toggle("lit", following);
  };
  liveMeters.push(cell);
  P.ptKeySrc.valueChangedEvent.addListener(cell.update);
  P.ptKeySrc.propertiesChangedEvent.addListener(cell.update);
  return cell;
}

/** Notes: the 12 notes (lit = Pitch may aim for it; click = switch it off), MIDI (Off / Notes / Learn)
    and Transpose. MIDI notes glow blue. */
function notesCell() {
  const cell = document.createElement("div");
  cell.className = "cell pt-notes";
  cell.innerHTML = `<span class="cell-label">Notes</span><span class="cell-sub">click = off</span><div class="note-grid"></div>
    <div class="key-src midi-src"></div><div class="tp"><button type="button" class="tp-dn">\u2212</button><span class="tp-v mono"></span><button type="button" class="tp-up">+</button></div>`;
  const grid = cell.querySelector(".note-grid");
  const notes = NOTES.map((t, k) => {
    const b = document.createElement("button");
    b.type = "button"; b.textContent = t;
    b.title = `Switch ${t} off / on`;
    b.addEventListener("click", () => { const id = PT_RM[k]; P[id].setValue(!on(id)); cell.update(); anyEdited(); });
    grid.appendChild(b);
    return b;
  });
  const midiBox = cell.querySelector(".midi-src");
  const midiBtns = ["MIDI off", "Notes", "Learn"].map((t, i) => {
    const b = document.createElement("button");
    b.type = "button"; b.textContent = t;
    b.addEventListener("click", () => { P.ptMidi.setChoiceIndex(i); cell.update(); anyEdited(); });
    midiBox.appendChild(b);
    return b;
  });
  const tpv = cell.querySelector(".tp-v");
  const step = (d) => { setScaled("ptTranspose", clamp(Math.round(val("ptTranspose")) + d, -12, 12)); cell.update(); anyEdited(); };
  cell.querySelector(".tp-dn").addEventListener("click", () => step(-1));
  cell.querySelector(".tp-up").addEventListener("click", () => step(1));
  cell.update = () => {
    const midi = choice("ptMidi");
    notes.forEach((b, k) => {
      const off = on(PT_RM[k]);
      b.classList.toggle("off", off);
      b.classList.toggle("in", !off && ((M.notesUsed >> k) & 1) === 1);
      b.classList.toggle("midi", midi > 0 && ((M.midiNotes >> k) & 1) === 1);
    });
    midiBtns.forEach((b, i) => b.classList.toggle("sel", i === midi));
    const t = Math.round(val("ptTranspose"));
    tpv.textContent = t === 0 ? "transpose 0" : `transpose ${t > 0 ? "+" : MINUS}${Math.abs(t)}`;
  };
  liveMeters.push(cell);
  [...PT_RM, "ptMidi", "ptTranspose"].forEach((id) => P[id].valueChangedEvent.addListener(cell.update));
  return cell;
}

/** BEAT mode: what this beat's key and tuning are (sent to the vocals for Pitch). */
function beatKeyCell() {
  const cell = document.createElement("div");
  cell.className = "cell pitch-cell bk-cell";
  cell.innerHTML = `<span class="cell-label">Beat Key</span><span class="cell-sub">sent to Pitch</span>
    <div class="pitch-face"><div class="pf-row"><span class="pf-target bk-name">\u2014</span></div><div class="pf-cents mono bk-info">listening</div></div>`;
  const name = cell.querySelector(".bk-name"), info = cell.querySelector(".bk-info");
  cell.update = () => {
    if (M.bkState !== 5) { name.textContent = "\u2026"; info.textContent = M.bkHeard > 0 ? `listening ${Math.round(M.bkHeard)} / 6 s` : "press play"; return; }
    name.textContent = beatKeyName(false);
    name.style.fontSize = name.textContent.length > 9 ? "15px" : name.textContent.length > 7 ? "18px" : "22px";
    const open = M.bkOpen >= 0 ? ` \u00B7 ${NOTES[(M.bkOpen + 11) % 12]} / ${NOTES[M.bkOpen]} open` : "";
    info.textContent = `${Math.round(M.bkConf * 100)} % sure \u00B7 tuned ${Math.abs(M.bkTune) < 3 ? "A440" : fmtTune(M.bkTune)}${open}`;
  };
  liveMeters.push(cell);
  return cell;
}

/** Pitch page: dims the knobs the chosen mode ignores (Robot: Retune / Humanize / Vibrato; Classic: Vibrato). */
function pitchCells() {
  const tag = (cell, cls) => { cell.classList.add(cls); return cell; };
  const cells = [
    tag(grid("ptMode", "Mode", "how it tunes", PITCH_MODES, 1), "pt-mode"),
    knob("ptAmount", "Amount", "how much", fmtPct, 0),
    knob("ptSpeed", "Retune", "how fast", fmtMs, 50),
    knob("ptVibrato", "Vibrato", "flat ↔ deeper", fmtVib, 0, true),
    knob("ptHumanize", "Humanize", "long notes live", fmtPct, 0),
    knob("ptFormant", "Formant", "deeper / thinner", fmtSt, 0, true),
    keyCell(),
    tag(grid("ptScale", "Scale", "allowed notes", SCALES_SHORT, 2), "pt-scale"),
    tag(pitchCell(), "pt-tune"),
    notesCell(),
  ];
  // Key, Scale, Tune, Notes: the status sits on the title line, bigger ("KEY \u00B7 beat: Bm").
  cells.slice(6).forEach((cell) => {
    const label = cell.querySelector(".cell-label"), sub = cell.querySelector(".cell-sub");
    const row = document.createElement("div");
    row.className = "cell-title";
    label.before(row);
    row.append(label, sub);
  });
  const [, , speed, vib, human, , , scaleCell] = cells;
  // Following the beat: the scale in use (your choice, or the beat's Major / Minor for Chromatic) glows gold.
  const scaleBtns = [...scaleCell.querySelectorAll(".vseg button")];
  scaleCell.update = () => {
    const following = M.bkState === 1;
    scaleCell.classList.toggle("following", following && M.scaleUsed !== choice("ptScale"));
    scaleBtns.forEach((b, i) => b.classList.toggle("beat", following && M.scaleUsed !== choice("ptScale") && i === M.scaleUsed));
  };
  liveMeters.push(scaleCell);
  const refresh = () => {
    const m = choice("ptMode");
    speed.classList.toggle("inactive", m === 2);
    human.classList.toggle("inactive", m === 2);
    vib.classList.toggle("inactive", m !== 0);
  };
  P.ptMode.valueChangedEvent.addListener(refresh);
  P.ptMode.propertiesChangedEvent.addListener(refresh);
  refresh();
  return cells;
}
const noteName = (m) => `${NOTES[((Math.round(m) % 12) + 12) % 12]}${Math.floor(Math.round(m) / 12) - 1}`;

/** Live pitch: the note you sing (with how far off, in cents) and the note it pulls to. */
function pitchCell() {
  const cell = document.createElement("div");
  cell.className = "cell pitch-cell";
  cell.innerHTML = `<span class="cell-label">Tune</span><span class="cell-sub">live</span>
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
  { key: "pitch", name: "PITCH", onId: "ptOn", what: "auto-tune: natural tuning to the full robot effect",
    cells: pitchCells,
    stat: () => (val("ptAmount") < 0.05 ? ["idle", false] : M.pitchTarget >= 0 ? [`\u2192 ${noteName(M.pitchTarget)}`, true]
      : [`${NOTES[M.keyUsed]} ${SCALES_SHORT[M.scaleUsed].split(" ")[0].toLowerCase()} · ${PITCH_MODES[choice("ptMode")].toLowerCase()}`, true]) },
  { key: "cleanup", name: "CLEANUP", onId: "clOn", what: "low cut, pops, breaths and gate: the recording cleaned up",
    cells: () => [
      knob("clLowCut", "Low Cut", "rumble below", (v) => (v <= 20.5 ? "Off" : fmtHz(v)), 20),
      knob("clPops", "Pops", "p & b thumps", (v) => (v < 0.05 ? "Off" : fmtPct(v)), 0),
      knob("clBreath", "Breaths", "turned down", (v) => (v < 0.05 ? "Off" : `${MINUS}${v.toFixed(1)} dB`), 0),
      knob("clGateThr", "Gate", "opens above", fmtDb, -60),
      knob("clGateRange", "Gate Range", "gaps turned down", (v) => (v < 0.05 ? "Off" : fmtDb(v)), 0),
      multiMeter("Working", "right now", [["P", () => M.pops, 24, () => val("clPops") >= 0.05], ["B", () => M.breath, 24, () => val("clBreath") >= 0.05],
        ["G", () => M.gate, 30, () => val("clGateRange") >= 0.05]]),
    ],
    stat: () => {
      if (M.clipNow >= 1) return ["clipped in!", true];
      if (M.overNow >= 1) return ["too hot in!", true];
      if (M.pops < -3) return [`pop ${fmtNum(M.pops, 0)} dB`, true];
      if (M.breath < -1) return [`breath ${fmtNum(M.breath, 0)} dB`, true];
      if (M.gate < -0.5) return [`gate ${fmtNum(M.gate, 0)} dB`, true];
      const parts = [val("clLowCut") > 20.5, val("clPops") >= 0.05, val("clBreath") >= 0.05, val("clGateRange") >= 0.05].filter(Boolean).length;
      return parts ? [val("clLowCut") > 20.5 ? `cut ${fmtHz(val("clLowCut"))}` : "ready", true] : ["idle", false];
    } },
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
  { key: "double", name: "VOICES", onId: "dbOn", what: "doubles and harmonies: stacked takes and backing voices in key",
    cells: () => [
      knob("dbAmount", "Double", "stacked take", fmtPct, 0),
      knob("dbWidth", "Width", "how far apart", fmtPct, 70),
      grid("hv1", "Voice 1", "harmony (left)", INTERVALS_SHORT, 2),
      grid("hv2", "Voice 2", "harmony (right)", INTERVALS_SHORT, 2),
      knob("hvLevel", "Level", "voices vs lead", fmtPct, 50),
      knob("hvFormant", "Formant", "voices' tone", fmtSt, 0, true),
    ],
    stat: () => {
      const notes = (M.hvNotes || []).filter((n) => n >= 0).map(noteName);
      if (notes.length) return [notes.join(" \u00B7 "), true];
      const h = [choice("hv1"), choice("hv2")].filter((i) => i > 0).map((i) => INTERVALS_SHORT[i]);
      if (h.length) return [h.join(" + "), true];
      return val("dbAmount") < 0.05 ? ["idle", false] : [`double ${fmtPct(val("dbAmount"))}`, true];
    } },
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
  if (/Gain|Thr|Target|Range|Peak|Makeup|Drive|Cut|Breath/.test(id)) return ["db", ""];
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

/** Several live bars in one cell: [letter, value (dB, <= 0), full scale (dB), in use?]. */
function multiMeter(label, sub, bars) {
  const cell = document.createElement("div");
  cell.className = "cell meter-cell dyn-meter";
  cell.innerHTML = `<span class="cell-label">${label}</span><span class="cell-sub">${sub}</span><div class="dyn-bars">${bars.map(([n]) =>
    `<div class="dyn-col"><div class="mbar"><i></i></div><span class="dyn-name mono">${n}</span></div>`).join("")}</div><span class="cell-value"></span>`;
  const is = [...cell.querySelectorAll(".mbar i")], cols = [...cell.querySelectorAll(".dyn-col")], v = cell.querySelector(".cell-value");
  cell.update = () => {
    let deepest = 0;
    bars.forEach(([, get, range, used], k) => {
      const x = get() || 0;
      deepest = Math.min(deepest, x);
      is[k].style.height = `${clamp(-x / range, 0, 1) * (is[k].parentElement.clientHeight - 6)}px`;
      cols[k].classList.toggle("off", !used());
    });
    v.textContent = deepest > -0.1 ? "0 dB" : fmtDb(deepest);
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
// BEAT mode: Unmask (its own page; the vocal chain's hive is hidden)
const UM_BANDS = [{ hz: 200, n: "200" }, { hz: 400, n: "400" }, { hz: 800, n: "800" }, { hz: 1600, n: "1.6k" }, { hz: 3150, n: "3.2k" }, { hz: 6300, n: "6.3k" }];
let umSources = { sources: [], selected: "" };
function sourceCell() {
  const cell = document.createElement("div");
  cell.className = "cell src";
  cell.innerHTML = `<span class="cell-label">Make room for</span><span class="cell-sub">which vocal</span><div class="src-list"></div>`;
  const box = cell.querySelector(".src-list");
  const make = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; e.textContent = text; return e; };
  cell.refresh = () => {
    const list = umSources.sources || [];
    box.replaceChildren();
    if (!list.length) { box.append(make("div", "none", "No vocal found yet. Put Voxology on your vocal (VOCAL mode) and press play.")); return; }
    const mk = (label, name) => {
      const b = make("button", (umSources.selected || "") === name ? "sel" : "", label);
      b.type = "button";
      b.addEventListener("click", async () => { await umSetSource(name); umSources.selected = name; cell.refresh(); });
      box.append(b);
    };
    mk(list.length > 1 ? `All vocals (${list.length})` : "All vocals", "");
    list.forEach((x) => mk(x.name, x.name));
  };
  cell.refresh();
  return cell;
}
const UNMASK = { key: "unmask", name: "UNMASK", onId: null, what: "the beat steps back where your vocal sings",
  cells: () => [
    knob("umAmount", "Amount", "how far back", fmtPct, 50),
    seg("umFocus", "Focus", "where it dips", ["Centre", "Full"]),
    sourceCell(),
    beatKeyCell(),
    Object.assign(multiMeter("Dipping", "right now", UM_BANDS.map((b, i) => [b.n, () => (M.umDip ? M.umDip[i] : 0), 6, () => val("umAmount") >= 0.05])), { className: "cell meter-cell dyn-meter um-meter" }),
  ],
  stat: () => [M.umLink > 0 ? "hearing vocal" : "no vocal", M.umLink > 0] };
const unmaskCells = UNMASK.cells();
let beat = false;
let lastLinkSeen = performance.now();   // the "no vocal" warning waits 2 s (the link needs a moment after loading)
const cur = () => (beat ? UNMASK : MODULES[selected]);

async function fetchSources() {
  try { umSources = JSON.parse((await umGetSources()) || "{}"); } catch { umSources = { sources: [], selected: "" }; }
  if (!umSources.sources) umSources.sources = [];
  unmaskCells.forEach((c) => c.classList.contains("src") && c.refresh());
}
setInterval(() => { if (beat) fetchSources(); }, 1000);

function applyMode() {
  const b = choice("mode") === 1;
  document.querySelectorAll("#mode button").forEach((x) => x.classList.toggle("sel", Number(x.dataset.m) === (b ? 1 : 0)));
  if (b === beat && cellsEl.dataset.page) return;
  beat = b;
  document.body.classList.toggle("beat", beat);
  $("chain-title").textContent = beat ? "BEAT" : "SIGNAL CHAIN";
  document.querySelector("#visual .caps").textContent = beat ? "BEAT SPECTRUM" : "VOCAL SPECTRUM";
  document.querySelector("#visual .legend").innerHTML = beat
    ? '<i class="sw in"></i>your beat <i class="sw out"></i>after Unmask <i class="sw dyn"></i>making room <i class="sw eq"></i>your vocal'
    : '<i class="sw in"></i>your vocal <i class="sw out"></i>after Voxology <i class="sw eq"></i>Tone EQ <i class="sw dyn"></i>Dynamic EQ';
  if (beat) {
    cellsEl.dataset.cols = "6";
    cellsEl.dataset.page = "unmask";
    cellsEl.replaceChildren(...unmaskCells);
    unmaskCells.forEach((c) => c.refresh && c.refresh());
    renderModuleHead();
    learnTab = "module";
    renderLearn();
    fetchSources();
  } else select(selected);
}
document.querySelectorAll("#mode button").forEach((x) => x.addEventListener("click", () => { P.mode.setChoiceIndex(Number(x.dataset.m)); applyMode(); }));
P.mode.valueChangedEvent.addListener(applyMode);
P.mode.propertiesChangedEvent.addListener(applyMode);

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
  const m = cur();
  $("mod-num").textContent = beat ? "BEAT" : String(selected + 1).padStart(2, "0");
  $("mod-name").textContent = m.name;
  $("mod-what").textContent = m.what;
  const pw = $("mod-power");
  pw.hidden = !m.onId;
  if (m.onId) { pw.textContent = on(m.onId) ? "ON" : "OFF"; pw.classList.toggle("on", on(m.onId)); }
  $("mod-kept").hidden = beat || !(report && report.ok && report.kept && report.kept[selected]);
}
$("mod-power").addEventListener("click", () => {
  const m = MODULES[selected];
  if (!m.onId) return;
  P[m.onId].setValue(!on(m.onId));
  renderModuleHead(); refreshHive(); anyEdited();
});
function select(i) {
  selected = i;
  if (beat) return;
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
  if (beat) {
    // The dips Unmask is making right now (orange), and where the vocal is (dots on the 0 dB line).
    const dips = (M.umDip || []).map((d) => -d);
    if (dips.some((c) => c >= 0.1)) {
      const sr = M.sr || 48000;
      ctx.beginPath();
      ctx.moveTo(0, yEq(0));
      for (let x = 0; x <= PW; x += 3) {
        const f = fFor(x);
        let db = 0;
        UM_BANDS.forEach((b, i) => { if (dips[i] > 1e-3) db += magDb(coeffs(1, b.hz, -dips[i], 1.9, sr), f, sr); });
        ctx.lineTo(x, yEq(db));
      }
      ctx.lineTo(PW, yEq(0)); ctx.closePath();
      ctx.fillStyle = "rgba(214,120,60,0.30)"; ctx.fill();
      ctx.strokeStyle = "#c8642c"; ctx.lineWidth = 2; ctx.stroke();
    }
    const peak = Math.max(...(M.umVocal || [-120]));
    UM_BANDS.forEach((b, i) => {
      const v = (M.umVocal || [])[i] ?? -120;
      const a = M.umLink > 0 ? clamp((v - (peak - 30)) / 30, 0, 1) : 0;
      if (a <= 0) return;
      ctx.beginPath(); ctx.arc(xFor(b.hz), yEq(0), 4 + 7 * a, 0, Math.PI * 2);
      ctx.fillStyle = `rgba(36,97,143,${0.25 + 0.55 * a})`; ctx.fill();
    });
    nodes.forEach((n) => (n.hidden = true));
    dynNodes.forEach((n) => (n.hidden = true));
    return;
  }
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

/** The incoming vocal: clipped or over full scale (Cleanup and Output pages). */
function inputTips() {
  const t = [];
  if (M.clipTotal > 0) t.push(tip("CLIPPED RECORDING", `The vocal arrives with flat-topped (clipped) peaks: ${M.clipTotal} so far. That's distortion baked in before Voxology, and it can crackle once the vocal is brightened and compressed.`, "warn",
    { need: "Yes if you hear crackle on loud words.", steps: ["Zoom in on the audio event in Cubase: flat tops mean it was recorded too hot. Re-record with the loudest words peaking around -10 dB.",
      "No flat tops in the event? Then something before Voxology on this track clips it (a plug-in above Voxology, or a bounce made through a limiter)."] }));
  if (M.hotPeak > 0.05) t.push(tip("TOO HOT", `The vocal reaches +${fmtNum(M.hotPeak, 1)} dB coming in: over the top of the meter. Cubase doesn't clip inside the mix, but a limiter later (like one on the Stereo Out) has to squash it hard, and that crackles.`, "warn",
    { need: "Yes. Leave room for the rest of the chain.", steps: ["Lower the vocal event's clip gain (or the plug-in above Voxology) until the loudest words peak around -6 dB here.", "Check your Stereo Out limiter only shaves 1 - 3 dB."] }));
  return t;
}

const LEARN = {
  pitch: {
    does: "Pitch correction (auto-tune). It hears the note you sing, picks the nearest note of your key, and pulls you onto it. Your voice's tone stays the same (no chipmunk sound); breaths and s sounds are never touched. Three modes: Natural (your voice, just in tune), Classic (the familiar auto-tune glide) and Robot (the hard, stepped trap effect). It's first in the chain, so everything after it hears the tuned voice.",
    how: ["Mode: Natural for a human-sounding lead (notes land on pitch; your vibrato and the start of a scoop stay). Robot for the T-Pain / melodic-trap effect: instant, flat, stepped notes whatever Retune says. Classic is in between and follows Retune.",
      "Key, Auto (the default): Pitch follows the key it hears. Surest: let Voxology hear your beat, either by sending the beat to this Voxology's side-chain (Cubase: side-chain button in the plug-in's top bar, then a Send from the beat track), or with a second Voxology on the beat in BEAT mode. It hears the beat's notes, home key and tuning. With no beat, it learns the key from the notes you sing (about 20 - 40 s of held notes; rap may never be sure). Your Scale choice picks the flavour within those notes (Minor = the minor home, Minor Penta = the 5 safest notes); Chromatic uses the key's own mode. Key / Scale are the fallback.",
      "Key, Manual: set Key / Scale to your beat's key (often in the beat's name, e.g. \"A min\"). Chromatic allows all 12 notes when you're not sure.",
      "Retune: how fast a note is pulled in. Natural: 10 - 40 ms (your vibrato stays at any speed). Classic: 0 - 10 ms is hard and robotic, 30 - 80 ms tuned but natural, 100+ ms only fixes drift.",
      "Vibrato (Natural): 0 keeps it as you sang it; turn it down to calm a wobbly note (all the way = flat), up to make it deeper.",
      "Humanize: lets long held notes keep their life while short notes still snap in.",
      "Notes: lit notes are the ones Pitch may pull you to. Click one to switch it off (it's never used), click again to bring it back.",
      "MIDI: in Cubase, make a MIDI track and set its output to Voxology. Notes = while you hold notes, your voice goes to those notes (play or draw the melody for the hard robot effect); nothing held = the key as usual. Learn = play the beat's notes or chords once and they become the scale (kept with the project).",
      "REC (header): Record mode for tracking. You hear yourself tuned with about 6 ms delay instead of 33. Slightly rougher; Voices and Formant are off until you switch it off.",
      "Transpose: moves the whole voice up or down in semitones (your tone stays the same). Small moves sound natural; an octave sounds like an effect.",
      "Amount: 100 % lands right on the note; lower keeps some of your own pitch.",
      "Already using Auto-Tune or Melodyne? Turn this off: one tuner is enough."],
    live: () => {
      const t = [];
      if (M.recMode) t.push(tip("RECORD MODE", `Voxology is in Record mode: about ${M.latencyMs.toFixed(0)} ms, so you can hear yourself tuned while you record. Pitch uses a faster, slightly rougher method; Voices and Formant are off.`, "calm",
        { need: "Switch it off when you're done recording.", steps: ["Click REC in the header to go back to full quality for mixing (Cubase adjusts the timing for you)."] }));
      if (choice("ptMidi") === 2 && M.midiNotes === 0) t.push(tip("LEARN IS WAITING", "MIDI is on Learn, but no notes have been played yet, so Pitch uses the key as usual.", "calm",
        { need: "Only if you want the MIDI scale.", steps: ["Route a MIDI track's output to Voxology (Cubase: the track's output menu).", "Play the beat's notes or chords once: they light up blue in Notes."] }));
      if (choice("ptKeySrc") === 0 && M.scState === 1 && M.bkSource !== 2 && M.inShort > -70) t.push(tip("SIDE-CHAIN SILENT",
        "Voxology's side-chain is switched on, but no sound is coming into it, so the key can't come from your beat" + (M.bkSource === 3 ? " (it's guessing from your voice instead)." : "."), "warn",
        { need: "Yes, if you want the beat's key.", steps: ["In Voxology's window, click the small arrow next to the side-chain button (top bar) and choose Add Side-Chain Source, then pick your beat track. (Or: on the beat track, add a Send whose destination is this vocal's Voxology side-chain.)", "Make sure that send is switched on (lit) and its level is up, around 0 dB, and the beat track isn't muted.", "Press play: within a few seconds the Key status should read \"beat: \u2026\". If it still says \"beat: no sound\", send me a screenshot of the beat track's Sends."] }));
      if (val("ptAmount") >= 0.05 && choice("ptKeySrc") === 0 && M.bkSource !== 1 && M.bkSource !== 2 && M.scState !== 1) t.push(tip("NO BEAT HEARD",
        `Key is on Auto, but Voxology can't hear your beat, so it learns the key from your singing${M.bkState === 1 ? ` (now: ${beatKeyName()})` : ": that takes about 20 - 40 s of held notes, and rap may never be sure"}. ${M.bkState === 1 ? "" : "Until then Pitch uses the Key / Scale below. "}The beat is the surest way.`, "calm",
        { need: "Optional, but it's the surest way to always be in the beat's key.", steps: ["Open Voxology on your vocal and click the side-chain button in the top bar of its window (Cubase), so it lights up.", "On your beat track, add a Send and pick this vocal's \"Voxology - Side-Chain\" as its destination, at 0 dB.", "Press play: after about 6 s the beat's key shows up here. (Or: put a second Voxology on the beat track and click BEAT.)"] }));
      if (val("ptAmount") >= 0.05 && M.bkState === 1 && Math.abs(M.bkTune) >= 10) t.push(tip("BEAT DETUNED", `Your beat is tuned ${fmtTune(M.bkTune)} away from standard (A = 440 Hz). Pitch tunes your notes to the beat's tuning, so they sit with it.`, "calm",
        { need: "No. It's handled.", steps: ["Nothing to do. (If you also use another tuner, set its reference to match.)"] }));
      if (val("ptAmount") >= 0.05 && M.bkState !== 1 && M.scaleUsed === 0) t.push(tip("CHROMATIC", "All 12 notes are allowed, so a wrong note can't be pulled into the key; it just gets cleaned up.", "calm",
        { need: "Optional. It works; the right key sounds tighter.", steps: ["Set Key and Scale to your beat's key (Auto-Edit suggests one in its report)."] }));
      if (val("ptAmount") >= 0.05 && M.pitchTarget >= 0 && Math.abs(M.pitchCorr) > 0.9) t.push(tip("BIG JUMP", `It's moving your voice ${Math.abs(M.pitchCorr * 100).toFixed(0)} cents right now. Big moves can sound warbly on held notes.`, "calm",
        { need: "Only if it sounds off. Big moves usually mean the Key / Scale doesn't match the beat.", steps: ["Check the Key / Scale match your beat.", "Or raise Retune to 40 ms or more for a softer pull (not in Robot mode)."] }));
      if (val("ptAmount") >= 0.05 && choice("ptMode") === 2 && M.scaleUsed === 0) t.push(tip("ROBOT + CHROMATIC", "Robot snaps to the nearest of all 12 notes, so in-between notes can land on notes outside your beat's key.", "warn",
        { need: "Yes for the classic effect: it needs the key.", steps: ["Put Voxology on your beat in BEAT mode (Key: Beat), or set Key and Scale to your beat's key."] }));
      return t;
    } },
  cleanup: {
    does: "Cleans up the recording before anything else. It also watches the vocal coming in and warns you if it arrives clipped or too hot. Low Cut removes rumble, hum and mic-stand bumps under the voice. Pops catches the low thump of \"p\" and \"b\" hitting the mic and cuts it for just those few hundredths of a second. Breaths turns your breaths down (they get louder later, when the compressor works) without touching the words. The gate turns the gaps between phrases down, so room noise and headphone bleed don't creep up.",
    how: ["Low Cut: raise it until the voice starts to thin, then back off 10 - 20 Hz. Deep voices sit around 70 - 90 Hz, higher voices 100 - 150 Hz.",
      "Pops: 60 - 100 % catches them. It only acts on a pop, so a high setting is safe.",
      "Breaths: 4 - 8 dB keeps them natural, 10 - 15 dB is the clean, tight rap sound. Off keeps every breath as recorded.",
      "Gate: set it between the noise in your gaps and your quietest words. Gate Range 10 - 15 dB sounds natural.",
      "The Working meter shows P (pop cut), B (breath turn-down) and G (gate) live."],
    live: () => {
      const t = [];
      t.push(...inputTips());
      if (val("clGateRange") >= 20) t.push(tip("DEEP GATE", `The gaps go down ${fmtNum(val("clGateRange"), 0)} dB. Breaths vanish completely, which can sound robotic.`, "calm",
        { need: "Optional. Fine if you like it dead-quiet between lines.", steps: ["Lower Gate Range to about 12 dB, and use Breaths at 8 - 10 dB instead for a cleaner but natural sound."] }));
      if (val("clBreath") >= 16) t.push(tip("NO AIR", `Breaths go down ${fmtNum(val("clBreath"), 0)} dB, so they're almost gone. Some breath keeps a vocal human, especially on sung parts.`, "calm",
        { need: "Optional: your call. Rap ad-libs often sound good this tight.", steps: ["Try 8 - 10 dB and compare with A / B."] }));
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
  unmask: {
    does: "This Voxology is on your BEAT. It listens to the Voxology on your vocal, and while you sing it turns the beat down a few dB only in the frequencies your voice is using right then. By default only the middle of the beat dips (where the vocal sits), so its width stays. Between your lines the beat comes straight back. Your vocal cuts through without turning it up.",
    how: ["Setup: Voxology on the vocal track (VOCAL mode, as usual) and another Voxology on the beat track (or the beat's group), switched to BEAT. Nothing to route: they find each other.",
      "Press play: \"Make room for\" lists the vocals it hears. All vocals is right for most songs; pick one to make room for the lead only.",
      "Amount: 30 - 60 % is felt more than heard; 100 % dips up to about 6 dB where words are understood (1.6 - 3 kHz).",
      "Focus: Centre dips only the middle of the beat (keeps it wide); Full dips the whole beat.",
      "Compare with A / B: A is your beat untouched, B is with the room made."],
    live: () => {
      const t = [];
      if (beat && performance.now() - lastLinkSeen > 2000 && !on("bypass")) t.push(tip("NO VOCAL HEARD", "This Voxology doesn't hear a Voxology on a vocal right now, so the beat isn't changed.", "warn",
        { need: "Yes, for Unmask to do anything.", steps: ["Put Voxology on your vocal track in VOCAL mode (the normal mode).", "Press play: both tracks need to be playing (a vocal Voxology on a muted or silent track sends nothing).",
          "Both must be in the same project. If your DAW runs plug-ins in separate processes (plug-in sandboxing), turn that off for Voxology."] }));
      if (beat && val("umAmount") >= 85) t.push(tip("DEEP DIPS", `Amount is ${fmtPct(val("umAmount"))}: the beat dips up to about ${fmtNum(val("umAmount") * 0.06, 1)} dB under the words. On busy beats you may hear it breathe.`, "calm",
        { need: "Optional. Fine if the beat still feels steady.", steps: ["Try 40 - 60 % and compare with A / B."] }));
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
    does: "Backing vocals from your own voice. Double stacks two slightly late copies left and right, like a second take. Voice 1 and Voice 2 sing harmonies with you, a 3rd, 5th or octave away, always in the key and scale set in the Pitch module, so they never clash. They get your lead's EQ and de-essing, follow its level, and go into the delay and reverb too.",
    how: ["Set Key and Scale in the Pitch module first (your beat's key): the harmonies use it. Chromatic gives plain intervals that can clash.",
      "Voice 1 / Voice 2: a 3rd up is the classic harmony; add a 5th down or an octave down for a full stack. Octave down is the deep ad-lib voice.",
      "Level: 30 - 50 % keeps them behind you. Width spreads Voice 1 left and Voice 2 right (and the double).",
      "Formant: - makes the voices deeper and darker (a different singer behind you), + thinner and brighter.",
      "Use harmonies on hooks and ad-libs rather than whole verses: automate the module's ON button in Cubase."],
    live: () => {
      const t = [];
      if ((choice("hv1") > 0 || choice("hv2") > 0) && choice("ptScale") === 0) t.push(tip("CHROMATIC KEY", "The Pitch module's scale is Chromatic, so the harmonies use plain intervals (a major 3rd), which can clash with a minor beat.", "warn",
        { need: "Yes, if the harmonies sound sour.", steps: ["Open 01 PITCH and set Key and Scale to your beat's key.", "Run Auto-Edit on a sung part: it suggests a key in its report."] }));
      return t;
    } },
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
      const t = inputTips();
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

function referenceBlock() {
  if (refInfo.state === "none") {
    body.append(head("REFERENCE MATCH"), para("Got a vocal you love the sound of? Load its acapella with + REFERENCE (under STYLE). Auto-Edit then aims your vocal at its tone, how bright its s sounds are, its punch and its space, instead of the style's built-in target."));
    body.append(tip("WHERE TO GET ONE", "Use the vocal on its own (an acapella or vocal stem), not the full song: a beat under it throws the reading off by about 7 dB.", "calm",
      { need: "Optional. The styles work without one.", steps: ["Search for \"<song name> acapella\" (many artists release them), or ask the producer for the vocal stem.", "WAV, AIFF, FLAC, MP3 or OGG all work. A verse or hook (10 s+) is plenty."] }));
    return;
  }
  body.append(head("REFERENCE MATCH"));
  if (refInfo.state === "loading") { body.append(para("Reading the reference…")); return; }
  if (refInfo.state === "problem") {
    body.append(tip("CAN'T USE THIS ONE", refInfo.problem || "This file can't be used as a reference.", "warn", { need: "Yes, to use Reference Match.", steps: ["Choose another file with + REFERENCE.", "Or remove it (✕) to use the style's target."] }));
    return;
  }
  body.append(para(`Auto-Edit will match "${refInfo.name}" (${Number(refInfo.seconds || 0).toFixed(0)} s of voice heard): its tone, s brightness, punch and space. Pitch, clean-up and saturation still follow the STYLE.`));
  if (refInfo.warning) body.append(tip("FULL SONG?", refInfo.warning, "warn",
    { need: "Yes, for a close match. It still works, roughly.", steps: ["Find the song's acapella or vocal stem and load that instead."] }));
}

function renderReport() {
  referenceBlock();
  if (report || refInfo.state !== "none") body.append(el("div", "l-sep"));
  if (!report) {
    body.append(head("AUTO-EDIT"), para("Auto-Edit listens to your vocal, sets every module for the style you pick and explains each choice here."));
    const ol = el("ol", "l-list");
    ["Pick a STYLE (Trap Lead, Rap, Melodic, Robot, R&B, Pop, Folk, Natural Singer) and an INTENSITY.", "Press AUTO-EDIT, then play a part of the song where you're rapping or singing.",
      "After about 12 seconds of voice it sets everything. Read why here, compare with A / B, and press UNDO if you don't like it."].forEach((s) => ol.append(el("li", "", s)));
    body.append(ol);
    return;
  }
  if (!report.ok) {
    body.append(head("AUTO-EDIT"), para(report.summary || ""));
    body.append(tip(report.tipTitle || "TRY AGAIN", report.tip || "Play a part where you're singing, then press Auto-Edit.", "warn"));
    return;
  }
  body.append(head("AUTO-EDIT REPORT"), para(`${report.style} · ${report.intensity}${report.reference ? ` · matched to "${report.reference}"` : ""}`, "l-meta"), para(report.summary));
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
  const m = cur(), L = LEARN[m.key];
  body.append(head(beat ? "UNMASK (BEAT MODE)" : `${String(selected + 1).padStart(2, "0")} ${m.name}`), para(L.does));
  if (m.onId && !on(m.onId)) body.append(tip("SWITCHED OFF", "This module is off, so it doesn't change your vocal.", "calm", { need: "No.", steps: ["Click ON at the top of the module (or the dot on its cell) to use it."] }));
  liveTips(m).forEach(([, v]) => body.append(v.el));
  body.append(head("HOW TO USE IT"), list(L.how));
  if (report && report.ok && !beat) {
    const rs = (report.reasons || []).filter((r) => r.module === m.key);
    if (rs.length) {
      body.append(head(report.kept && report.kept[selected] ? "AUTO-EDIT LOOKED · KEPT" : "WHAT AUTO-EDIT DID HERE"));
      rs.forEach((r) => body.append(item(r.control, r.value, r.why)));
    }
  }
}

// Live tips come and go with the meters many times a second. Each one stays at least 4 s once it
// shows (a newer reading of the same tip just updates it in place), so the panel never flickers.
const stickyTips = new Map();   // module + title -> { el, until }
function liveTips(m) {
  const now = performance.now();
  for (const t of LEARN[m.key].live()) {
    const id = m.key + "|" + t.querySelector(".t-title").textContent;
    const entry = stickyTips.get(id);
    if (!entry) { stickyTips.set(id, { el: t, until: now + 4000 }); continue; }
    if (entry.el.textContent !== t.textContent) {
      if (entry.el.isConnected) entry.el.replaceWith(t);   // new numbers, same place
      entry.el = t;
    }
    entry.until = now + 4000;
  }
  for (const [id, v] of stickyTips) if (v.until < now) stickyTips.delete(id);
  return [...stickyTips].filter(([id]) => id.startsWith(m.key + "|"));
}

let learnKey = "";
function renderLearn(force = true) {
  // Rebuild only when which tips are shown changes (their text updates in place).
  const m = cur();
  const tips = learnTab === "module" ? liveTips(m) : [];
  const key = learnTab + (beat ? "beat" : selected) + tips.map(([id]) => id).join("|") + (m.onId ? on(m.onId) : "") + (report ? report.time : "");
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
function refreshStyle() {
  const name = STYLES[choice("aeStyle")] || STYLES[0];
  $("style-name").textContent = name;
  $("style-name").classList.toggle("long", name.length > 10);
}
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
  $("rec").classList.toggle("on", on("recMode"));
  document.body.classList.toggle("rec", on("recMode"));
  $("bypass").classList.toggle("off", on("bypass"));
}
abBtns.forEach((b) => b.addEventListener("click", () => { P.listenA.setValue(b.dataset.ab === "a"); refreshAB(); }));
$("match").addEventListener("click", () => { P.levelMatch.setValue(!on("levelMatch")); refreshAB(); });
$("rec").addEventListener("click", () => { P.recMode.setValue(!on("recMode")); refreshAB(); });
$("bypass").addEventListener("click", () => { P.bypass.setValue(!on("bypass")); refreshAB(); });
for (const id of ["listenA", "levelMatch", "bypass", "recMode"]) P[id].valueChangedEvent.addListener(refreshAB);

$("auto-edit").addEventListener("click", async () => {
  if (M.aeState === 1) { await aeCancel(); return; }
  if (M.aeState === 0) {
    const ok = await aeStart();
    if (ok) { M.aeState = 1; M.aeProgress = 0; refreshAutoEdit(); }
  }
});
// Reference Match
async function fetchReference() {
  try { refInfo = JSON.parse((await refGet()) || "{}"); } catch { refInfo = { state: "none" }; }
  if (!refInfo.state) refInfo.state = "none";
  const b = $("ref");
  b.classList.toggle("ok", refInfo.state === "ok");
  b.classList.toggle("warn", refInfo.state === "problem" || !!refInfo.warning);
  b.textContent = refInfo.state === "loading" ? "READING…" : refInfo.state === "none" ? "+ REFERENCE"
    : `${refInfo.state === "ok" ? "\u266A" : "\u26A0"} ${refInfo.name || "reference"}`;
  b.title = refInfo.state === "none" ? "Reference Match: load the acapella of a vocal you love, and Auto-Edit aims at its sound"
    : refInfo.problem || refInfo.warning || `Auto-Edit will match "${refInfo.name}". Click to choose another.`;
  $("ref-clear").hidden = refInfo.state === "none" || refInfo.state === "loading";
  if (learnTab === "report") renderLearn();
}
$("ref").addEventListener("click", async () => { await refChoose(); });
$("ref-clear").addEventListener("click", async () => { await refClear(); fetchReference(); });

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

let reportVersion = -1, refVersionSeen = -1;
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
  if (M.umLink > 0 || !beat) lastLinkSeen = performance.now();
  if (beat) {
    $("beat-stat").textContent = M.umLink > 0 ? `hearing ${M.umLink > 1 ? M.umLink + " vocals" : "your vocal"}` : "no vocal yet";
    $("beat-text").textContent = M.umLink > 0 ? "Your vocal is linked. While you sing, the beat steps back where your voice is (orange on the spectrum)."
      : "Put Voxology on your vocal track too (VOCAL mode) and press play. They find each other: nothing to route.";
  }
  if (M.refVersion !== refVersionSeen) { refVersionSeen = M.refVersion; fetchReference(); }
  if (M.aeReport !== reportVersion) { const first = reportVersion === -1; reportVersion = M.aeReport; fetchReport(!first); }
  const match = (on("levelMatch") ? ` · MATCH ${fmtSigned(M.matchDb)}` : "") + (M.recMode ? ` · REC ${M.latencyMs.toFixed(1)} ms` : "");
  $("status").textContent = `${on("bypass") ? "BYPASSED" : on("listenA") ? "A: ORIGINAL" : "B: VOXOLOGY"}${match}${M.bpm > 0 ? ` · ${M.bpm.toFixed(0)} BPM` : ""}`;
  draw();
  if (learnTab === "module") renderLearn(false);
});

// Refresh everything once the parameter values have arrived.
for (const id of [...SLIDERS, ...TOGGLES, ...COMBOS]) P[id].valueChangedEvent.addListener(() => { refreshHive(); });
select(0);
setTimeout(() => { refreshAll(); applyMode(); }, 100);
draw();
