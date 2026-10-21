// Sounds: recorded guitar, ukulele and bass notes (public/samples; the mandolin plays the guitar's
// with its doubled strings), a soft piano, cajon strokes, and a metronome click. Until an instrument's recordings
// have loaded, and if they can't, plucked strings are synthesized (Karplus–Strong) instead.
// Synthesized notes are rendered once into AudioBuffers and cached, so strumming costs little.
import type { Instrument } from "../../shared/types";
import { midiToFreq } from "../../shared/music/notes";
import { SAMPLES } from "./sampleMap";

let ctx: AudioContext | null = null;
let master: GainNode | null = null;
const cache = new Map<string, AudioBuffer>();

export function audioContext(): AudioContext {
  if (!ctx) {
    ctx = new AudioContext({ latencyHint: "interactive" });
    const comp = ctx.createDynamicsCompressor();
    comp.threshold.value = -14;
    comp.ratio.value = 4;
    master = ctx.createGain();
    master.gain.value = 0.8;
    master.connect(comp).connect(ctx.destination);
  }
  return ctx;
}

/** Call from a click / key handler: browsers only start audio after a user gesture. */
export async function unlockAudio(): Promise<AudioContext> {
  const c = audioContext();
  if (c.state !== "running") await c.resume().catch(() => {});
  return c;
}

function out(): GainNode {
  audioContext();
  return master!;
}

function pluckBuffer(midi: number, instrument: Instrument): AudioBuffer {
  const key = `p${instrument}${midi}`;
  const hit = cache.get(key);
  if (hit) return hit;
  const c = audioContext();
  const sr = c.sampleRate;
  const dur = instrument === "mandolin" ? 1.6 : instrument === "ukulele" ? 1.4 : instrument === "bass" ? 3 : 2.6;
  const buf = c.createBuffer(1, Math.floor(sr * dur), sr);
  const data = buf.getChannelData(0);
  const period = sr / midiToFreq(midi);
  const n = Math.max(2, Math.round(period));
  const ring = new Float32Array(n);
  // pick position / brightness by instrument
  const bright = instrument === "mandolin" ? 0.9 : instrument === "ukulele" ? 0.55 : instrument === "bass" ? 0.3 : 0.7;
  let last = 0;
  for (let i = 0; i < n; i++) {
    const noise = Math.random() * 2 - 1;
    last = last + bright * (noise - last); // low-passed noise burst = softer pick
    ring[i] = last;
  }
  const decay = instrument === "bass" ? 0.998 : instrument === "guitar" ? 0.996 : 0.992;
  let idx = 0;
  let prev = 0;
  for (let i = 0; i < data.length; i++) {
    const cur = ring[idx];
    const next = ring[(idx + 1) % n];
    const v = decay * 0.5 * (cur + next);
    ring[idx] = v;
    data[i] = cur * 0.6 + prev * 0.4;
    prev = cur;
    idx = (idx + 1) % n;
  }
  // gentle fade-out to avoid a click
  const fade = Math.floor(sr * 0.05);
  for (let i = 0; i < fade; i++) data[data.length - 1 - i] *= i / fade;
  cache.set(key, buf);
  return buf;
}

function pianoBuffer(midi: number): AudioBuffer {
  const key = `k${midi}`;
  const hit = cache.get(key);
  if (hit) return hit;
  const c = audioContext();
  const sr = c.sampleRate;
  const dur = 2.4;
  const buf = c.createBuffer(1, Math.floor(sr * dur), sr);
  const data = buf.getChannelData(0);
  const f = midiToFreq(midi);
  const partials = [1, 0.55, 0.3, 0.18, 0.1, 0.06];
  for (let i = 0; i < data.length; i++) {
    const t = i / sr;
    let v = 0;
    for (let p = 0; p < partials.length; p++) {
      const fp = f * (p + 1) * (1 + 0.0004 * p * p); // slight inharmonicity
      if (fp > sr / 2) break;
      v += partials[p] * Math.exp(-t * (1.2 + p * 0.9)) * Math.sin(2 * Math.PI * fp * t);
    }
    const attack = Math.min(1, t / 0.004);
    data[i] = 0.35 * v * attack;
  }
  const fade = Math.floor(sr * 0.05);
  for (let i = 0; i < fade; i++) data[data.length - 1 - i] *= i / fade;
  cache.set(key, buf);
  return buf;
}

function playBuffer(buf: AudioBuffer, when: number, gain: number, stopAt?: number) {
  const c = audioContext();
  const src = c.createBufferSource();
  src.buffer = buf;
  const g = c.createGain();
  g.gain.value = gain;
  src.connect(g).connect(out());
  src.start(Math.max(when, c.currentTime));
  if (stopAt !== undefined) {
    g.gain.setValueAtTime(gain, Math.max(stopAt - 0.06, when));
    g.gain.linearRampToValueAtTime(0.0001, stopAt);
    src.stop(stopAt + 0.01);
  }
}

// ---- recorded notes ----

type Bank = keyof typeof SAMPLES;
const BANK_OF: Partial<Record<Instrument, Bank>> = { guitar: "guitar", mandolin: "guitar", ukulele: "ukulele", bass: "bass" };

interface Recorded {
  /** the pitch it sounds at (MIDI, with its tuning correction) */
  pitch: number;
  buf: AudioBuffer;
  /** seconds of silence before the attack (decoders may add a little) */
  start: number;
}
const banks = new Map<Bank, Recorded[]>();
const loading = new Map<Bank, Promise<void>>();

/** Where a decoded note starts sounding. */
function attackAt(buf: AudioBuffer): number {
  const d = buf.getChannelData(0);
  let peak = 0;
  for (let i = 0; i < d.length; i++) peak = Math.max(peak, Math.abs(d[i]));
  let i = 0;
  while (i < d.length && Math.abs(d[i]) < 0.02 * peak) i++;
  return Math.max(0, i - 0.002 * buf.sampleRate) / buf.sampleRate;
}

/** Loads an instrument's recorded notes (once; a failed load is tried again next time). */
export function loadInstrument(instrument: Instrument): Promise<void> {
  const bank = BANK_OF[instrument];
  if (!bank) return Promise.resolve();
  let p = loading.get(bank);
  if (!p) {
    const c = audioContext();
    p = Promise.all(
      SAMPLES[bank].map(async (n) => {
        const res = await fetch(`/samples/${bank}/${n.file}`);
        if (!res.ok) throw new Error(`${n.file}: ${res.status}`);
        const buf = await c.decodeAudioData(await res.arrayBuffer());
        return { pitch: n.midi + n.cents / 100, buf, start: attackAt(buf) };
      }),
    ).then(
      (notes) => void banks.set(bank, notes),
      () => void loading.delete(bank),
    );
    loading.set(bank, p);
  }
  return p;
}

interface Voice {
  when: number;
  gain: number;
  /** lowpass cutoff (Hz): a softer stroke sounds darker, not just quieter */
  tone: number;
  stopAt?: number;
  /** let it ring at most this long */
  ring?: number;
  /** cents above the note (a mandolin's second string) */
  detune?: number;
}

function playRecorded(notes: Recorded[], midi: number, v: Voice) {
  const c = audioContext();
  let n = notes[0];
  for (const x of notes) if (Math.abs(x.pitch - midi) < Math.abs(n.pitch - midi)) n = x;
  const src = c.createBufferSource();
  src.buffer = n.buf;
  src.playbackRate.value = 2 ** ((midi - n.pitch + (v.detune ?? 0) / 100) / 12);
  const lp = c.createBiquadFilter();
  lp.type = "lowpass";
  lp.frequency.value = v.tone;
  lp.Q.value = 0.4;
  const g = c.createGain();
  src.connect(lp).connect(g).connect(out());
  const at = Math.max(v.when, c.currentTime);
  g.gain.setValueAtTime(v.gain, at);
  src.start(at, n.start);
  const end = Math.min(v.stopAt ?? Infinity, v.ring !== undefined ? at + v.ring : Infinity);
  if (end < Infinity) {
    // a hand coming down on the strings: quick, but not a click
    g.gain.setValueAtTime(v.gain, Math.max(at, end - 0.07));
    g.gain.exponentialRampToValueAtTime(0.0005, end);
    src.stop(end + 0.02);
  }
}

/** A little human unevenness, so repeated strums don't sound like a machine. */
const jitter = (x: number, by: number) => x * (1 + (Math.random() * 2 - 1) * by);

/** One plucked string of a fretted instrument, starting at `when`. */
function pluck(instrument: Instrument, midi: number, when: number, vel: number, stopAt?: number) {
  const bank = BANK_OF[instrument];
  const notes = bank && banks.get(bank);
  if (!notes) {
    void loadInstrument(instrument);
    playBuffer(pluckBuffer(midi, instrument), when, vel * 0.75, stopAt);
    return;
  }
  const v = jitter(vel, 0.08);
  if (instrument === "mandolin") {
    // two steel strings per course, a hair apart in time and tune; it doesn't ring for long
    const tone = 3500 + 9000 * v * v;
    playRecorded(notes, midi, { when, gain: 0.62 * v, tone, stopAt, ring: 1.8 });
    playRecorded(notes, midi, { when: when + 0.007, gain: 0.5 * v, tone, stopAt, ring: 1.8, detune: 6 });
    return;
  }
  const tone = instrument === "bass" ? 700 + 3000 * v * v : instrument === "ukulele" ? 2500 + 9000 * v * v : 1800 + 10000 * v * v;
  playRecorded(notes, midi, { when, gain: (instrument === "bass" ? 1.1 : 0.9) * v, tone, stopAt });
}

export interface StrumOptions {
  instrument: Instrument;
  /** seconds between strings (guitar strum) */
  spread?: number;
  down?: boolean;
  velocity?: number;
  /** stop (mute) time, for chords that change */
  until?: number;
}

/** Plays notes as a strum (fretted) or a block chord (piano), starting at AudioContext time `when`. */
export function strum(midis: number[], when: number, o: StrumOptions) {
  if (!midis.length) return;
  const vel = o.velocity ?? 0.8;
  if (o.instrument === "piano") {
    const g = (vel * 0.9) / Math.sqrt(midis.length);
    for (const m of midis) playBuffer(pianoBuffer(m), when, g, o.until);
    return;
  }
  const spread = o.spread ?? 0.018;
  const down = o.down !== false;
  const order = down ? midis : [...midis].reverse();
  const g = vel / Math.sqrt(midis.length);
  order.forEach((m, i) => {
    // the pick meets the first strings hardest; an up-stroke barely catches the low ones
    const fall = order.length > 1 ? i / (order.length - 1) : 0;
    const accent = down ? 1 - 0.18 * fall : 0.8 + 0.2 * fall;
    pluck(o.instrument, m, when + jitter(i * spread, 0.15), g * accent, o.until);
  });
}

/** Plays notes one after another (single notes / riffs). */
export function arpeggio(midis: number[], when: number, step: number, instrument: Instrument) {
  midis.forEach((m, i) => {
    const stop = when + (i + 1) * step + 0.4;
    if (instrument === "piano") playBuffer(pianoBuffer(m), when + i * step, 0.75, stop);
    else pluck(instrument, m, when + i * step, 0.8, stop);
  });
}

/** Metronome tick. Accent = first beat of the bar. */
export function click(when: number, accent: boolean, volume = 0.6) {
  const c = audioContext();
  const osc = c.createOscillator();
  const g = c.createGain();
  osc.frequency.value = accent ? 1760 : 1175;
  g.gain.setValueAtTime(0.0001, when);
  g.gain.exponentialRampToValueAtTime(volume, when + 0.002);
  g.gain.exponentialRampToValueAtTime(0.0001, when + 0.06);
  osc.connect(g).connect(out());
  osc.start(when);
  osc.stop(when + 0.08);
}

// ---- cajon ----

export type CajonStroke = "B" | "S" | "t";

/** Seeded noise, so each stroke renders the same every time. */
function noise(seed: number) {
  let s = seed >>> 0;
  return () => ((s = (s * 1103515245 + 12345) >>> 0) / 2 ** 31 - 1);
}

/** A two-pole band-pass (RBJ) for shaping noise. */
function bandPass(sr: number, lo: number, hi: number) {
  const hz = Math.sqrt(lo * hi);
  const w = (2 * Math.PI * hz) / sr;
  const alpha = Math.sin(w) / (2 * (hz / (hi - lo)));
  const a0 = 1 + alpha;
  const a1 = -2 * Math.cos(w);
  const a2 = 1 - alpha;
  let x1 = 0, x2 = 0, y1 = 0, y2 = 0;
  return (x: number) => {
    const y = (alpha * x - alpha * x2 - a1 * y1 - a2 * y2) / a0;
    x2 = x1;
    x1 = x;
    y2 = y1;
    y1 = y;
    return y;
  };
}

/**
 * A cajon stroke, rendered once: the Bass is a deep thump with the box's wood ringing; the Slap a
 * sharp crack with the snare wires' buzz; the tap a light ghost note. Two takes of each, so a run
 * of strokes doesn't sound copied.
 */
function cajonBuffer(stroke: CajonStroke, take: number): AudioBuffer {
  const key = `cajon:${stroke}${take}`;
  const hit = cache.get(key);
  if (hit) return hit;
  const c = audioContext();
  const sr = c.sampleRate;
  const dur = stroke === "B" ? 0.45 : stroke === "S" ? 0.32 : 0.14;
  const buf = c.createBuffer(1, Math.floor(sr * dur), sr);
  const data = buf.getChannelData(0);
  const rnd = noise(17 + take * 101 + stroke.charCodeAt(0));
  const tune = 1 + (take ? 0.03 : 0);
  if (stroke === "B") {
    const low = bandPass(sr, 40, 400);
    let phase = 0;
    for (let i = 0; i < data.length; i++) {
      const t = i / sr;
      phase += (2 * Math.PI * (52 + 75 * Math.exp(-t / 0.022)) * tune) / sr;
      const body = Math.sin(phase) * Math.exp(-t / 0.12);
      const wood = 0.18 * Math.sin(2 * Math.PI * 175 * tune * t) * Math.exp(-t / 0.035);
      const thump = 0.9 * low(rnd()) * Math.exp(-t / 0.008);
      data[i] = body + wood + thump;
    }
  } else if (stroke === "S") {
    const crack = bandPass(sr, 1500, 7000);
    const wires = bandPass(sr, 2500, 9000);
    for (let i = 0; i < data.length; i++) {
      const t = i / sr;
      const body = 0.35 * Math.sin(2 * Math.PI * 330 * tune * t) * Math.exp(-t / 0.03) + 0.25 * Math.sin(2 * Math.PI * 215 * tune * t) * Math.exp(-t / 0.05);
      data[i] = 2.2 * crack(rnd()) * Math.exp(-t / 0.022) + 0.7 * wires(rnd()) * Math.exp(-t / 0.075) + body;
    }
  } else {
    const click = bandPass(sr, 900, 4000);
    const wires = bandPass(sr, 3000, 9000);
    for (let i = 0; i < data.length; i++) {
      const t = i / sr;
      data[i] = click(rnd()) * Math.exp(-t / 0.01) + 0.25 * wires(rnd()) * Math.exp(-t / 0.03);
    }
  }
  // a 1 ms attack (no click), the level set per stroke, a short fade at the end
  let peak = 1e-9;
  for (const v of data) peak = Math.max(peak, Math.abs(v));
  const level = { B: 0.95, S: 0.8, t: 0.3 }[stroke] / peak;
  const attack = Math.floor(sr * 0.001);
  const fade = Math.floor(sr * 0.02);
  for (let i = 0; i < data.length; i++) {
    data[i] *= level * Math.min(1, i / attack) * Math.min(1, (data.length - 1 - i) / fade);
  }
  cache.set(key, buf);
  return buf;
}

/** Plays a cajon stroke. */
export function cajonHit(stroke: CajonStroke, when: number, velocity = 0.9) {
  playBuffer(cajonBuffer(stroke, Math.random() < 0.5 ? 0 : 1), when, velocity);
}

/** Pre-renders the buffers a song needs so the first strum isn't late. */
export function warmUp(midis: number[], instrument: Instrument) {
  if (instrument === "cajon") {
    for (const s of ["B", "S", "t"] as const) for (const take of [0, 1]) cajonBuffer(s, take);
    return;
  }
  if (instrument === "piano") for (const m of midis) pianoBuffer(m);
  else void loadInstrument(instrument);
}
