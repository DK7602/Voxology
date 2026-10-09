// Browser preview only. Inside the plug-in, JUCE provides window.__JUCE__ before this runs and this
// file does nothing. Opened in a normal browser it simulates the plug-in, so the UI can be designed
// and tested without a DAW.
(function () {
  if (typeof window.__JUCE__ !== "undefined") return;

  const lin = (start, end, value, interval = 0.1) => ({ start, end, skew: 1, interval, value });
  const centre = (start, end, c, value, interval = 1) => ({ start, end, skew: Math.log(0.5) / Math.log((c - start) / (end - start)), interval, value });
  const sliders = {
    ptAmount: lin(0, 100, 0), ptSpeed: centre(0, 400, 60, 50), ptHumanize: lin(0, 100, 0), ptFormant: lin(-6, 6, 0), ptVibrato: lin(-100, 100, 0, 1), ptTranspose: lin(-12, 12, 0, 1),
    hvLevel: lin(0, 100, 50), hvFormant: lin(-6, 6, 0),
    clLowCut: centre(20, 400, 80, 20), clGateThr: lin(-80, -20, -60), clGateRange: lin(0, 30, 0), clPops: lin(0, 100, 0), clBreath: lin(0, 24, 0),
    dsAmount: lin(0, 100, 0), dsSens: lin(0, 100, 50), dsFreq: centre(3000, 12000, 6000, 6000, 10),
    rdTarget: lin(-40, -6, -20), rdRange: lin(0, 12, 0),
    cpPeak: lin(-40, 0, 0), cpThr: lin(-60, 0, 0), cpRatio: centre(1, 10, 3, 1, 0.01), cpMakeup: lin(0, 24, 0), cpMix: lin(0, 100, 100),
    saDrive: lin(0, 18, 0), saMix: lin(0, 100, 50), dbAmount: lin(0, 100, 0), dbWidth: lin(0, 100, 70),
    dlFeedback: lin(0, 90, 25), dlMix: lin(0, 100, 0), dlTone: centre(1000, 16000, 4000, 6000, 10), dlDuck: lin(0, 100, 50),
    rvDecay: centre(0.3, 8, 1.5, 1.6, 0.01), rvPredelay: lin(0, 200, 20, 1), rvMix: lin(0, 100, 0), rvTone: centre(2000, 16000, 6000, 7000, 10),
    rvDuck: lin(0, 100, 30), outGain: lin(-24, 24, 0), umAmount: lin(0, 100, 50),
  };
  [[80, 400, 180], [150, 800, 300], [500, 2000, 900], [2000, 8000, 4000], [6000, 18000, 12000]].forEach(([lo, hi, def], i) => {
    sliders["eqGain" + (i + 1)] = lin(-12, 12, 0);
    sliders["eqFreq" + (i + 1)] = centre(lo, hi, Math.sqrt(lo * hi), def);
  });
  sliders.dqSens = lin(0, 100, 50);
  [[80, 300, 150], [200, 800, 350], [600, 2500, 1000], [2000, 8000, 3500]].forEach(([lo, hi, def], i) => {
    sliders["dqCut" + (i + 1)] = lin(0, 12, 0);
    sliders["dqFreq" + (i + 1)] = centre(lo, hi, Math.sqrt(lo * hi), def);
  });
  const toggles = { recMode: true, ptRm0: false, ptRm1: false, ptRm2: false, ptRm3: false, ptRm4: false, ptRm5: false, ptRm6: true, ptRm7: false, ptRm8: false, ptRm9: false, ptRm10: false, ptRm11: false, bypass: false, listenA: false, levelMatch: false, ptOn: true, clOn: true, eqOn: true, dqOn: true, dsOn: true, rdOn: true, cpOn: true, saOn: true, dbOn: true, dlOn: true, dlPing: false, rvOn: true };
  const combos = {
    aeStyle: { choices: ["Trap Lead", "Rap", "Melodic", "Robot", "R&B", "Pop", "Folk", "Natural Singer"], value: 0 },
    aeIntensity: { choices: ["Light", "Balanced", "Strong"], value: 0.5 },
    ptKey: { choices: ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"], value: 0 },
    ptScale: { choices: ["Chromatic", "Major", "Minor", "Harmonic Minor", "Minor Pentatonic", "Major Pentatonic", "Dorian", "Phrygian", "Mixolydian", "Blues"], value: 0 },
    ptMode: { choices: ["Natural", "Classic", "Robot"], value: 0 },
    ptKeySrc: { choices: ["Auto", "Manual"], value: 0 },
    ptMidi: { choices: ["Off", "Notes", "Learn"], value: 0.5 },
    rdSpeed: { choices: ["Slow", "Medium", "Fast"], value: 0.5 },
    saMode: { choices: ["Tape", "Tube", "Clip"], value: 0 },
    dlTime: { choices: ["1/4", "1/8", "1/8 dot", "1/4 dot", "1/16", "1/2"], value: 0 },
    mode: { choices: ["Vocal", "Beat"], value: location.hash === "#beat" ? 1 : 0 },
    umFocus: { choices: ["Centre", "Full"], value: 0 },
    hv1: { choices: ["Off", "3rd up", "5th up", "Octave up", "3rd down", "4th down", "5th down", "Octave down"], value: 1 / 7 },
    hv2: { choices: ["Off", "3rd up", "5th up", "Octave up", "3rd down", "4th down", "5th down", "Octave down"], value: 6 / 7 },
  };

  const send = (id, obj) => setTimeout(() => window.__JUCE__.backend && window.__JUCE__.backend.emitByBackend(id, JSON.stringify(obj)), 0);
  const setSlider = (k, v) => { sliders[k].value = v; send("__juce__slider" + k, { eventType: "valueChanged", value: v }); };
  const setToggle = (k, v) => { toggles[k] = v; send("__juce__toggle" + k, { eventType: "valueChanged", value: v }); };
  const setCombo = (k, i) => { const c = combos[k]; c.value = i / (c.choices.length - 1); send("__juce__comboBox" + k, { eventType: "valueChanged", value: c.value }); };

  // Simulated Auto-Edit (listening runs 3x faster than the real 12 s). Values from the engine's own
  // report on the synthetic test vocal (tools/report.cpp).
  const um = { selected: "" };
  const ref = { json: JSON.stringify({ state: "none" }), version: 0, yours: ["My Favorite Hook (Acapella)"] };
  const ae = { state: 0, progress: 0, undo: false, version: 0, report: "", snapshot: null };
  function aeFinish() {
    ae.snapshot = { sliders: JSON.parse(JSON.stringify(sliders)), toggles: { ...toggles }, combos: JSON.parse(JSON.stringify(combos)) };
    const v = { ptAmount: 100, ptSpeed: 10, ptHumanize: 20, clLowCut: 95, clGateThr: -40, clGateRange: 12, clPops: 80, clBreath: 8, eqGain1: 1.3, eqGain2: -1.1, eqFreq2: 630, eqGain4: 1.4, eqGain5: -3, dqCut1: 3, dqFreq1: 125, dqCut2: 4.5, dqFreq2: 400, dqCut4: 3.5, dqFreq4: 3150, dsAmount: 63, dsFreq: 5400,
      rdTarget: -24, rdRange: 2, cpPeak: -14, cpThr: -27, cpRatio: 4, cpMakeup: 7, saDrive: 3, saMix: 50, dbAmount: 30, dbWidth: 80,
      dlFeedback: 22, dlMix: 14, dlTone: 5000, dlDuck: 60, rvDecay: 1.4, rvPredelay: 30, rvMix: 12, rvTone: 6500, rvDuck: 40, outGain: 1.5 };
    for (const [k, x] of Object.entries(v)) setSlider(k, x);
    setCombo("ptKey", 9); setCombo("ptScale", 2);
    setCombo("saMode", 1); setCombo("rdSpeed", 1); setCombo("dlTime", 0);
    const R = (module, control, value, why) => ({ module, control, value, why });
    ae.report = JSON.stringify({
      ok: true, style: "Trap Lead", intensity: "Balanced", time: new Date().toISOString(), kept: [false, false, false, false, false, false, false, false, false, false, false, false],
      summary: "Listened to 12.0 s of voice (Trap Lead, Balanced). Your vocal came in at −23.7 LUFS with peaks at −4.8 dB. The chain is set for a Trap Lead sound: upfront, bright and controlled, with a short wide space around it. Every change is explained below.",
      notes: ["NOISY RECORDING: the noise in your gaps is only 28 dB under your voice, so very soft words and breaths sit close to it.\nNEED: Optional. The gate handles most of it; listen to quiet word endings.\nSTEP: If soft words get cut off, lower the Gate threshold 3 dB at a time (Cleanup module).\nSTEP: Next take: turn off fans / AC, record closer to the mic (a fist away), keep headphones quieter so they don't leak."],
      tips: ["Compare with A / B and MATCH on: MATCH plays both at the same loudness, so you judge the tone, not the volume.",
        "Using Auto-Tune or Melodyne? Put it BEFORE Voxology in the insert list, so the tuner hears the dry voice.",
        "Several vocal tracks? Turn Delay and Reverb off here and use one shared FX send instead: it glues the stack together and saves CPU."],
      reasons: [
        R("pitch", "Key", "A minor", "Your sung notes fit A minor best (78 % sure), so notes are pulled only to notes of that scale. If your beat is in another key, change Key / Scale in the Pitch module: the beat's key always wins."),
        R("pitch", "Retune", "10 ms, Humanize 20 %", "For Trap Lead: a tight, modern trap tune: notes snap in, long notes keep a little life. Lower = more robotic, higher = more natural."),
        R("pitch", "Amount", "100 %", "You sing on average 21 cents away from the nearest note (normal for a take: the tune tightens it)."),
        R("cleanup", "Low Cut", "95 Hz", "Your lowest notes sit around 130 Hz, so everything under 95 Hz is rumble, mic handling and pops, not voice. Cutting it cleans the low end for the 808 and kick."),
        R("cleanup", "Gate", "−40.0 dB, 12.0 dB down", "The gaps between your phrases have noise at −58.1 dB (room, interface hiss or headphone bleed), 32 dB under your voice. The gate turns those gaps down 12 dB, so the compressor and saturation don't bring that noise up."),
        R("cleanup", "Pops", "80 %", "Auto-Edit heard 9 pops (the low thump of a \"p\" or \"b\" hitting the mic, up to 18 dB over your voice's normal low end). The remover cuts that thump for the few hundredths of a second it lasts, and leaves the rest of the word alone."),
        R("cleanup", "Breaths", "−8 dB", "Auto-Edit heard 39 breaths, about 9 dB under your voice. The compressor and saturation later in the chain bring quiet sounds up, so breaths would get louder. They're turned down 8 dB, so the gaps between lines stay clean and the words hit harder. Words and \"s\" sounds are left alone."),
        R("eq", "Body", "+1.3 dB at 180 Hz", "Your voice is 3.0 dB thinner down low than a finished Trap Lead vocal, so a little body adds warmth and weight."),
        R("eq", "Mud", "−1.1 dB at 630 Hz", "There's a build-up around 630 Hz (3.2 dB over a finished vocal) that makes it sound boxy or cloudy. A narrow cut there cleans it without thinning the voice."),
        R("eq", "Nasal", "0 dB", "No honky / nasal peak (800 Hz - 1.6 kHz)."),
        R("eq", "Presence", "+1.4 dB at 4.0 kHz", "Your vocal is 2.8 dB short of a finished Trap Lead vocal in the presence range, where the words live. This lifts it so lyrics are clear over the beat."),
        R("eq", "Air", "−3.0 dB above 12.0 kHz", "Your top end is 6.8 dB brighter than the target, so it's eased down a little to avoid fizz."),
        R("dyneq", "Boom", "up to −3.0 dB at 125 Hz", "Some words jump out around 125 Hz: boom: the low end swells on some words (singing close to the mic, low notes, p and b sounds). The loudest of those moments rise about 3.1 dB past your voice's normal there (about 9 % of the time). The band cuts up to 3.0 dB only while that happens; the rest of the time it does nothing."),
        R("dyneq", "Mud", "up to −4.5 dB at 400 Hz", "Some words jump out around 400 Hz: mud: some vowels (\"oh\", \"oo\") cloud up and sound boxy. The loudest of those moments rise about 4.4 dB past your voice's normal there (about 12 % of the time). The band cuts up to 4.5 dB only while that happens; the rest of the time it does nothing."),
        R("dyneq", "Nasal", "Off", "Your mids (800 Hz - 2.0 kHz) stay steady from word to word, so there's nothing to catch here."),
        R("dyneq", "Harsh", "up to −3.5 dB at 3.2 kHz", "Some words jump out around 3.2 kHz: harshness: loud notes and shouted words get piercing. The loudest of those moments rise about 3.6 dB past your voice's normal there (about 7 % of the time). The band cuts up to 3.5 dB only while that happens; the rest of the time it does nothing."),
        R("dyneq", "Sensitivity", "50 %", "A band is pulled back once it rises 3.0 dB past how it usually sits in your voice. It learns that from your voice as it plays, so it works the same on quiet and loud lines."),
        R("deess", "Amount", "63 % at 5.4 kHz", "Your loudest \"s\" and \"t\" sounds peak at +1.5 dB vs your voice (around 6.3 kHz), which is sharp on headphones and earbuds. 63 % brings them to −4.1 dB, where a finished Trap Lead vocal sits, and only while they happen."),
        R("rider", "Range", "±2.0 dB, Medium", "Your loud and quiet lines are 10.3 dB apart. The rider turns quiet words up and loud ones down toward −24.0 dB, like riding a fader."),
        R("comp", "Peak", "−14.0 dB", "Catches the sudden loud syllables: about 5.0 dB off the loudest 5 % of moments, so nothing jumps out of the beat."),
        R("comp", "Level", "−27.0 dB, 4.0:1", "Smooths the whole performance by about 4.0 dB on average, the amount a Trap Lead vocal usually gets."),
        R("comp", "Makeup", "+7.0 dB", "Puts back the level the compressor took away, so you compare tone, not loudness."),
        R("sat", "Mode", "Tube", "For Trap Lead, Tube adds warmth and a rich, forward low-mid."),
        R("sat", "Drive", "3.0 dB, Mix 50 %", "Set so the added harmonics sit around −32.0 dB under your voice: felt more than heard."),
        R("double", "Amount", "30 %, Width 80 %", "Adds two drifting copies left and right, like a stacked double take. The lead stays centred."),
        R("delay", "Time", "1/4 at 140 BPM", "Echoes on the beat grid fill the gaps between lines. Duck 60 % keeps them quiet while you're rapping."),
        R("delay", "Mix", "14 %, Feedback 22 %", "Low enough to feel, not hear as a separate echo."),
        R("reverb", "Decay", "1.4 s, Pre-delay 30 ms", "A medium plate that gives the vocal a place to live."),
        R("reverb", "Mix", "12 %", "Just enough space to sound like a record without washing out the words."),
        R("out", "Output", "+1.5 dB", "Matches the processed vocal to the loudness it came in at (−23.7 LUFS), so it doesn't jump in your mix."),
      ],
    });
    ae.undo = true; ae.version++; ae.state = 0;
  }
  setInterval(() => { if (ae.state === 1) { ae.progress = Math.min(1, ae.progress + 0.1 / 4); if (ae.progress >= 1) { ae.state = 2; setTimeout(aeFinish, 900); } } }, 100);

  window.__JUCE__ = {
    initialisationData: {
      __juce__platform: [], __juce__functions: ["startAutoEdit", "cancelAutoEdit", "undoAutoEdit", "getAutoEditReport", "chooseReference", "clearReference", "getReference", "listReferences", "selectReference", "deleteReference", "getUnmaskSources", "setUnmaskSource", "getUiRed", "setUiRed"],
      __juce__registeredGlobalEventIds: [], __juce__sliders: Object.keys(sliders), __juce__toggles: Object.keys(toggles), __juce__comboBoxes: Object.keys(combos),
    },
    postMessage(message) {
      const { eventId, payload } = JSON.parse(message);
      if (eventId.startsWith("__juce__slider")) {
        const name = eventId.slice(14), s = sliders[name];
        if (payload.eventType === "requestInitialUpdate") {
          send(eventId, { eventType: "propertiesChanged", start: s.start, end: s.end, skew: s.skew, name, label: "", numSteps: 1000, interval: s.interval, parameterIndex: 0 });
          send(eventId, { eventType: "valueChanged", value: s.value });
        } else if (payload.eventType === "valueChanged") s.value = payload.value;
      } else if (eventId.startsWith("__juce__toggle")) {
        const name = eventId.slice(14);
        if (payload.eventType === "requestInitialUpdate") send(eventId, { eventType: "valueChanged", value: toggles[name] });
        else if (payload.eventType === "valueChanged") toggles[name] = payload.value;
      } else if (eventId.startsWith("__juce__comboBox")) {
        const name = eventId.slice(16), c = combos[name];
        if (payload.eventType === "requestInitialUpdate") {
          send(eventId, { eventType: "propertiesChanged", name, parameterIndex: 0, choices: c.choices });
          send(eventId, { eventType: "valueChanged", value: c.value });
        } else if (payload.eventType === "valueChanged") c.value = payload.value;
      } else if (eventId === "__juce__invoke") {
        let result = true;
        if (payload.name === "startAutoEdit") { ae.state = 1; ae.progress = 0; }
        if (payload.name === "cancelAutoEdit" && ae.state === 1) ae.state = 0;
        if (payload.name === "getAutoEditReport") result = ae.report;
        if (payload.name === "chooseReference") { ref.json = JSON.stringify({ state: "loading" }); ref.version++;
          setTimeout(() => { ref.json = JSON.stringify({ state: "ok", name: "Favorite Artist - Hook (Acapella)", seconds: 41, problem: "", warning: "" }); ref.version++; }, 1200); }
        if (payload.name === "clearReference") { ref.json = JSON.stringify({ state: "none" }); ref.version++; }
        if (payload.name === "getReference") result = ref.json;
        if (payload.name === "getUiRed") result = !!window.__mockRed;
        if (payload.name === "setUiRed") window.__mockRed = !!payload.params[0];
        if (payload.name === "listReferences") result = JSON.stringify({ builtin: [
            { name: "Pro male singer", about: "Low voices that mostly sing" }, { name: "Pro male rap / rhythmic", about: "Low voices that rap" },
            { name: "Pro female singer, bright", about: "Airy, open top" }, { name: "Pro female singer, warm", about: "Smooth, soft top" },
            { name: "Pro rap, full songs (male)", about: "17 released rap vocals" }, { name: "Pro pop singer (male)", about: "15 released pop vocals" },
            { name: "Pro pop singer (female)", about: "26 released pop vocals" }, { name: "Pro electronic singer (female)", about: "13 dance vocals" },
            { name: "Pro female rap / rhythmic", about: "Present mids" }, { name: "Pro average (all voices)", about: "Neutral" } ],
          yours: ref.yours.map((n) => ({ name: n })), folder: "C:\\Users\\you\\Documents\\Voxology\\References", current: (JSON.parse(ref.json).name || "") });
        if (payload.name === "selectReference") { const n = payload.params[1]; ref.json = JSON.stringify({ state: "ok", name: n, seconds: 60, problem: "", warning: "" }); ref.version++; }
        if (payload.name === "deleteReference") { ref.yours = ref.yours.filter((n) => n !== payload.params[0]); ref.version++; }
        if (payload.name === "getUnmaskSources") result = JSON.stringify({ sources: [{ name: "Lead Vocal" }, { name: "Ad-libs" }], selected: um.selected });
        if (payload.name === "setUnmaskSource") um.selected = (payload.params && payload.params[0]) || "";
        if (payload.name === "undoAutoEdit") {
          result = ae.undo;
          if (ae.snapshot) {
            for (const [k, s] of Object.entries(ae.snapshot.sliders)) setSlider(k, s.value);
            for (const [k, v] of Object.entries(ae.snapshot.toggles)) setToggle(k, v);
          }
          ae.undo = false; ae.report = ""; ae.version++;
        }
        send("__juce__complete", { promiseId: payload.resultId, result });
      }
    },
  };

  // --- fake vocal --------------------------------------------------------------------------
  let t = 0;
  const shape = (f) => {   // a vocal-ish spectrum: body, formants, falling top
    const lo = f < 90 ? -18 * Math.log2(90 / f) : 0;
    const hi = f > 3000 ? -9 * Math.log2(f / 3000) : 0;
    return -34 + lo + hi + 4 * Math.exp(-Math.pow(Math.log2(f / 500), 2) * 3) + 3 * Math.exp(-Math.pow(Math.log2(f / 2700), 2) * 6);
  };
  setInterval(() => {
    t += 1 / 30;
    const word = Math.max(0, Math.sin(t * Math.PI * 2 * 1.6)), sing = word > 0.05 ? 1 : 0;
    const level = -26 + 8 * word;
    const active = (k) => !toggles.bypass && toggles[k];
    const inS = [], outS = [];
    for (let i = 0; i < 96; i++) {
      const f = 20 * Math.pow(1000, i / 95);
      const v = shape(f) + (sing ? 6 * word : -30) + (Math.random() - 0.5) * 3;
      inS.push(+v.toFixed(1)); outS.push(+(v + (toggles.bypass ? 0 : 3 + sliders.outGain.value)).toFixed(1));
    }
    const s = (k) => sliders[k].value;
    if (window.__JUCE__.backend) window.__JUCE__.backend.emitByBackend("voxMeters", JSON.stringify({
      inShort: +(-24 + 2 * Math.sin(t / 3)).toFixed(1), outShort: +(-23 + 2 * Math.sin(t / 3) + s("outGain")).toFixed(1),
      inPeak: +(level + 9).toFixed(1), outPeak: +(level + 8 + s("outGain")).toFixed(1),
      pops: active("clOn") && s("clPops") > 0 && word > 0.05 && word < 0.2 ? +(-s("clPops") / 100 * 18).toFixed(1) : 0,
      clipNow: 2, clipTotal: 42, overNow: 0, hotPeak: 3.4,
      breath: active("clOn") && s("clBreath") > 0 && !sing ? -s("clBreath") : 0,
      gate: active("clOn") && s("clGateRange") > 0 && !sing ? -s("clGateRange") : 0,
      umDip: [0.35, 0.6, 0.85, 1, 1, 0.7].map((w) => (combos.mode.value > 0.5 && sing ? +(-w * s("umAmount") / 100 * 6 * word).toFixed(1) : 0)),
      umVocal: [-38, -30, -26, -24, -28, -36].map((v) => (sing ? v + 6 * word : -120)),
      umLink: combos.mode.value > 0.5 ? 2 : 0,
      hvNotes: sing && toggles.dbOn ? [60, 50] : [-1, -1],
      dyn: [0, 1, 2, 3].map((b) => (active("dqOn") && s("dqCut" + (b + 1)) > 0 && sing ? +(-Math.min(s("dqCut" + (b + 1)), s("dqCut" + (b + 1)) * Math.max(0, Math.sin(t * (2.1 + b * 0.7) + b)) ** 3)).toFixed(1) : 0)),
      deEss: active("dsOn") && s("dsAmount") > 0 && word > 0.9 ? +(-s("dsAmount") / 100 * 7).toFixed(1) : 0,
      rider: active("rdOn") && s("rdRange") > 0 ? +(s("rdRange") * Math.sin(t / 2)).toFixed(1) : 0,
      peakGr: active("cpOn") && s("cpPeak") < 0 ? +(-5 * word * word).toFixed(1) : 0,
      levelGr: active("cpOn") && s("cpRatio") > 1.01 ? +(-4 * word).toFixed(1) : 0,
      satHarm: active("saOn") && s("saDrive") > 0 ? +(-42 + s("saDrive") * 3 + 2 * word).toFixed(1) : -100,
      matchDb: -1.2, bpm: 140, sr: 48000,
      bkState: location.hash === "#beat" ? 5 : 1, bkKey: 7, bkMode: 7, bkSet: 0, bkUnclear: 0, bkConf: 0.86, bkTune: -28, bkHeard: 24, keyUsed: 11, scaleUsed: 2, midiNotes: (1 << 11) | (1 << 2), notesUsed: 0b110010101101 & ~(1 << 6), recMode: 1, latencyMs: 6.1,
      pitchSung: sing ? +(57 + 0.25 * Math.sin(t * 3)).toFixed(2) : 0, pitchTarget: sing ? 57 : -1,
      pitchCorr: sing && sliders.ptAmount.value > 0 ? +(-0.25 * Math.sin(t * 3) * sliders.ptAmount.value / 100).toFixed(2) : 0,
      aeState: ae.state, aeProgress: +ae.progress.toFixed(2), aeHearing: true, aeUndo: ae.undo, aeReport: ae.version, refVersion: ref.version,
      in: inS, out: outS,
    }));
  }, 1000 / 30);
})();
