// Synthetic "songs" with known beats, downbeats and chords, for testing the chord / beat detector.
// Harmonic-rich wavetable instruments (pads, piano, strummed guitar, bass, a sung-ish melody) over
// a drum kit, with optional noise and mistuning, so the detector meets harmonics, drums and
// non-chord tones rather than pure sine triads.

export interface SongSpec {
  bpm: number;
  beatsPerBar?: number;
  /** chord per bar, or "G:2" for a chord lasting 2 beats */
  chords: string[];
  repeats?: number;
  /** chords played once after the repeats (e.g. the tonic to end on) */
  ending?: string[];
  sampleRate?: number;
  /** seconds of silence before the first downbeat */
  start?: number;
  /** seconds of silence after the music */
  tail?: number;
  /** global detune of every pitched instrument, in cents */
  tuningCents?: number;
  kick?: "all" | "1and3" | "none";
  snare?: boolean;
  hats?: boolean;
  instrument?: "pad" | "piano" | "guitar";
  /** overdrive on the chord instrument (tanh drive; 0 = clean), like a distorted rock guitar */
  distortion?: number;
  /** guitar only: strum loudness for each 16th note of a repeating cycle (8 values = 2 beats, 16 = a bar; 0 = rest),
   *  e.g. [1, 0.2, 0.2, 0.9, 0.2, 0.2, 0.9, 0.3] for an accented 3+3+2 pattern */
  strum?: number[];
  bass?: "beats" | "eighths" | "none";
  melody?: boolean;
  /** a picked guitar riff in 8th notes: semitones above each chord's root (the root at C4..B4) */
  riff?: number[];
  /** white-noise level (the mix peaks near 1) */
  noise?: number;
  seed?: number;
}

export interface SynthSong {
  samples: Float32Array;
  sampleRate: number;
  bpm: number;
  beatsPerBar: number;
  /** true beat times, beat 0 = first downbeat */
  beatTimes: number[];
  /** sounding chord on each beat */
  chordAtBeat: string[];
  /** the melody's notes (when there is one) */
  melodyNotes: { time: number; midi: number }[];
  duration: number;
}

const SUFFIX: Record<string, number[]> = {
  "": [0, 4, 7],
  "5": [0, 7],
  m: [0, 3, 7],
  "7": [0, 4, 7, 10],
  maj7: [0, 4, 7, 11],
  m7: [0, 3, 7, 10],
  sus4: [0, 5, 7],
  dim: [0, 3, 6],
};
const LETTER: Record<string, number> = { C: 0, D: 2, E: 4, F: 5, G: 7, A: 9, B: 11 };

/** "Ebm7" -> root 3, intervals [0, 3, 7, 10]; "N.C." -> null. */
export function parseTestChord(name: string): { root: number; intervals: number[] } | null {
  const m = /^([A-G])(#|b)?(.*)$/.exec(name);
  if (!m) return null;
  const root = (LETTER[m[1]] + (m[2] === "#" ? 1 : m[2] === "b" ? -1 : 0) + 12) % 12;
  const intervals = SUFFIX[m[3]];
  if (!intervals) throw new Error(`unknown chord suffix in ${name}`);
  return { root, intervals };
}

function mulberry32(seed: number): () => number {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

const TABLE = 4096;
const tables = new Map<string, Float32Array>();
/** One period of a waveform with the given harmonic amplitudes (cached). */
function wavetable(harmonics: number[]): Float32Array {
  const key = harmonics.map((h) => h.toFixed(4)).join(",");
  let t = tables.get(key);
  if (!t) {
    t = new Float32Array(TABLE + 1);
    for (let i = 0; i <= TABLE; i++) {
      let s = 0;
      for (let h = 0; h < harmonics.length; h++) s += harmonics[h] * Math.sin((2 * Math.PI * (h + 1) * i) / TABLE);
      t[i] = s;
    }
    tables.set(key, t);
  }
  return t;
}

type Timbre = (h: number) => number;
const TIMBRES: Record<string, Timbre> = {
  pad: (h) => Math.pow(0.62, h - 1), // soft, sawtooth-ish
  piano: (h) => 1 / h ** 1.1,
  guitar: (h) => (h === 1 ? 0.8 : 1 / h),
  bass: (h) => [1, 0.55, 0.3, 0.18, 0.1, 0.06][h - 1] ?? 0,
  voice: (h) => [1, 0.7, 0.45, 0.25, 0.12, 0.08][h - 1] ?? 0,
};

function harmonicsFor(timbre: string, freq: number, sr: number): number[] {
  const out: number[] = [];
  for (let h = 1; h <= 10 && h * freq < 0.45 * sr; h++) out.push(TIMBRES[timbre](h));
  return out;
}

interface NoteOpts {
  attack: number;
  /** exponential decay time constant (Infinity = sustained) */
  decay: number;
  release: number;
  vibratoCents?: number;
}

function addNote(buf: Float32Array, sr: number, t0: number, dur: number, freq: number, amp: number, timbre: string, o: NoteOpts, phase0 = 0): void {
  const table = wavetable(harmonicsFor(timbre, freq, sr));
  const i0 = Math.max(0, Math.round(t0 * sr));
  const i1 = Math.min(buf.length, Math.round((t0 + dur + o.release) * sr));
  let phase = phase0 * TABLE;
  const attackN = Math.max(1, o.attack * sr);
  const endN = dur * sr;
  const relN = Math.max(1, o.release * sr);
  for (let i = i0; i < i1; i++) {
    const n = i - i0;
    let env = n < attackN ? n / attackN : 1;
    if (o.decay !== Infinity) env *= Math.exp(-n / (o.decay * sr));
    if (n > endN) env *= Math.max(0, 1 - (n - endN) / relN);
    let f = freq;
    if (o.vibratoCents) f *= Math.pow(2, (o.vibratoCents * Math.sin((2 * Math.PI * 5.5 * n) / sr)) / 1200);
    const p = phase | 0;
    const frac = phase - p;
    buf[i] += amp * env * (table[p] + (table[p + 1] - table[p]) * frac);
    phase += (f * TABLE) / sr;
    if (phase >= TABLE) phase -= TABLE;
  }
}

const midiFreq = (m: number, cents: number) => 440 * Math.pow(2, (m - 69 + cents / 100) / 12);

export function synthSong(spec: SongSpec): SynthSong {
  const sr = spec.sampleRate ?? 22050;
  const bpb = spec.beatsPerBar ?? 4;
  const spb = 60 / spec.bpm;
  const start = spec.start ?? 0;
  const rand = mulberry32(spec.seed ?? 1);
  const cents = spec.tuningCents ?? 0;

  // chord per beat
  const seq: string[] = [];
  const reps = spec.repeats ?? 1;
  const push = (list: string[]) => {
    for (const c of list) {
      const [name, beats] = c.split(":");
      const n = beats ? parseInt(beats, 10) : bpb;
      for (let i = 0; i < n; i++) seq.push(name);
    }
  };
  for (let r = 0; r < reps; r++) push(spec.chords);
  if (spec.ending) push(spec.ending);
  const nBeats = seq.length;
  const beatTimes = Array.from({ length: nBeats }, (_, i) => start + i * spb);
  const duration = start + nBeats * spb + (spec.tail ?? 1);
  const buf = new Float32Array(Math.ceil(duration * sr));

  // ---- drums ----
  const kick = spec.kick ?? "all";
  for (let i = 0; i < nBeats; i++) {
    const t = beatTimes[i];
    const pos = i % bpb;
    if (kick === "all" || (kick === "1and3" && pos % 2 === 0)) {
      const i0 = Math.round(t * sr);
      let ph = 0;
      for (let n = 0; n < 0.3 * sr && i0 + n < buf.length; n++) {
        const tt = n / sr;
        const f = 45 + 70 * Math.exp(-tt / 0.03);
        ph += (2 * Math.PI * f) / sr;
        const click = n < 0.003 * sr ? (rand() * 2 - 1) * 0.5 : 0;
        buf[i0 + n] += (0.7 * Math.sin(ph) * Math.exp(-tt / 0.12) + click) * (pos === 0 ? 1 : 0.85);
      }
    }
    if (spec.snare && pos % 2 === 1) {
      const i0 = Math.round(t * sr);
      for (let n = 0; n < 0.25 * sr && i0 + n < buf.length; n++) {
        const tt = n / sr;
        buf[i0 + n] += 0.3 * (rand() * 2 - 1) * Math.exp(-tt / 0.07) + 0.15 * Math.sin(2 * Math.PI * 185 * tt) * Math.exp(-tt / 0.05);
      }
    }
    if (spec.hats) {
      for (const sub of [0, 0.5]) {
        const i0 = Math.round((t + sub * spb) * sr);
        let prev = 0;
        for (let n = 0; n < 0.06 * sr && i0 + n < buf.length; n++) {
          const w = rand() * 2 - 1;
          buf[i0 + n] += 0.12 * (w - prev) * Math.exp(-n / (0.015 * sr));
          prev = w;
        }
      }
    }
  }

  // ---- harmony ----
  const instrument = spec.instrument ?? "pad";
  const melodyNotes: { time: number; midi: number }[] = [];
  // the chord instrument plays into its own track when it is overdriven
  const drive = spec.distortion ?? 0;
  const chordBuf = drive > 0 ? new Float32Array(buf.length) : buf;
  const bassMode = spec.bass ?? "beats";
  let i = 0;
  while (i < nBeats) {
    let j = i;
    while (j < nBeats && seq[j] === seq[i]) j++;
    const chord = parseTestChord(seq[i]);
    const t0 = beatTimes[i];
    const dur = (j - i) * spb;
    if (chord) {
      // close voicing with the root between C3 and B3, plus the root an octave up
      const rootMidi = 48 + chord.root;
      const notes = chord.intervals.map((iv) => rootMidi + iv).concat([rootMidi + 12]);
      for (const m of notes) {
        const f = midiFreq(m, cents);
        if (instrument === "pad") {
          for (const det of [-6, 6]) {
            addNote(chordBuf, sr, t0, dur, f * Math.pow(2, det / 1200), 0.05, "pad", { attack: 0.03, decay: 4, release: 0.04 }, rand());
          }
        } else if (instrument === "piano") {
          for (let b = i; b < j; b += 2) {
            addNote(chordBuf, sr, beatTimes[b], Math.min(2, j - b) * spb, f, 0.08, "piano", { attack: 0.004, decay: 0.9, release: 0.05 }, rand());
          }
        } else {
          // strummed on every beat (or to the 16th-note pattern), low to high with 12 ms between strings
          const k = notes.indexOf(m);
          if (spec.strum) {
            for (let q = i * 4; q < j * 4; q++) {
              const amp = spec.strum[q % spec.strum.length];
              if (!amp) continue;
              const up = q % 2 === 1; // up-strums run high to low
              const kk = up ? notes.length - 1 - k : k;
              addNote(chordBuf, sr, start + (q / 4) * spb + 0.008 * kk, spb / 2, f, 0.08 * amp, "guitar", { attack: 0.003, decay: 0.35, release: 0.03 }, rand());
            }
          } else {
            for (let b = i; b < j; b++) {
              addNote(chordBuf, sr, beatTimes[b] + 0.012 * k, spb, f, b === i ? 0.08 : 0.06, "guitar", { attack: 0.003, decay: 0.7, release: 0.03 }, rand());
            }
          }
        }
      }
      if (bassMode !== "none") {
        const bassMidi = 36 + chord.root; // C2 .. B2
        const step = bassMode === "eighths" ? 0.5 : 1;
        for (let b = i; b < j; b += step) {
          // walk to the fifth on the off-beats of eighth-note bass
          const m = bassMode === "eighths" && b % 1 !== 0 && rand() < 0.4 ? bassMidi + 7 : bassMidi;
          addNote(buf, sr, start + b * spb, step * spb * 0.95, midiFreq(m, cents), 0.32, "bass", { attack: 0.005, decay: 0.5, release: 0.03 });
        }
      }
      if (spec.riff) {
        for (let b = i, k = 0; b < j; b += 0.5, k++) {
          const m = 60 + chord.root + spec.riff[k % spec.riff.length];
          addNote(buf, sr, start + b * spb, 0.5 * spb, midiFreq(m, cents), 0.1, "guitar", { attack: 0.003, decay: 0.45, release: 0.03 }, rand());
          melodyNotes.push({ time: start + b * spb, midi: m });
        }
      }
      if (spec.melody) {
        // chord tones on the beats, a scale step between, sung with vibrato an octave or two up
        for (let b = i; b < j; b += 0.5) {
          if (rand() < 0.2) continue;
          const onBeat = b % 1 === 0;
          let m = 72 + chord.root + chord.intervals[Math.floor(rand() * chord.intervals.length)];
          if (!onBeat && rand() < 0.5) m += rand() < 0.5 ? 2 : -1; // passing tone
          while (m > 81) m -= 12;
          addNote(buf, sr, start + b * spb, 0.5 * spb, midiFreq(m, cents), 0.09, "voice", { attack: 0.04, decay: Infinity, release: 0.05, vibratoCents: 25 });
          melodyNotes.push({ time: start + b * spb, midi: m });
        }
      }
    }
    i = j;
  }

  if (drive > 0) {
    // soft clipping: the notes' harmonics and their sums and differences (the distortion's own
    // "thirds" among them), at about the same loudness as the clean guitar
    const gain = 0.3 / Math.tanh(drive * 0.3);
    for (let n = 0; n < buf.length; n++) buf[n] += gain * Math.tanh(drive * chordBuf[n]);
  }

  if (spec.noise) for (let n = 0; n < buf.length; n++) buf[n] += spec.noise * (rand() * 2 - 1);

  // silence before the start (noise and release tails included)
  const s0 = Math.round(start * sr);
  for (let n = 0; n < s0; n++) buf[n] = 0;
  let peak = 0;
  for (let n = 0; n < buf.length; n++) peak = Math.max(peak, Math.abs(buf[n]));
  if (peak > 0) for (let n = 0; n < buf.length; n++) buf[n] *= 0.9 / peak;

  return { samples: buf, sampleRate: sr, bpm: spec.bpm, beatsPerBar: bpb, beatTimes, chordAtBeat: seq, melodyNotes, duration };
}
