// Sounds: plucked strings (Karplus–Strong), a soft piano, and a metronome click.
// Notes are rendered once into AudioBuffers and cached, so strumming costs almost nothing.
import type { Instrument } from "../../shared/types";
import { midiToFreq } from "../../shared/music/notes";

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
  const dur = instrument === "mandolin" ? 1.6 : instrument === "ukulele" ? 1.4 : 2.6;
  const buf = c.createBuffer(1, Math.floor(sr * dur), sr);
  const data = buf.getChannelData(0);
  const period = sr / midiToFreq(midi);
  const n = Math.max(2, Math.round(period));
  const ring = new Float32Array(n);
  // pick position / brightness by instrument
  const bright = instrument === "mandolin" ? 0.9 : instrument === "ukulele" ? 0.55 : 0.7;
  let last = 0;
  for (let i = 0; i < n; i++) {
    const noise = Math.random() * 2 - 1;
    last = last + bright * (noise - last); // low-passed noise burst = softer pick
    ring[i] = last;
  }
  const decay = instrument === "guitar" ? 0.996 : 0.992;
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
  const order = o.down === false ? [...midis].reverse() : midis;
  const g = vel / Math.sqrt(midis.length);
  order.forEach((m, i) => playBuffer(pluckBuffer(m, o.instrument), when + i * spread, g, o.until));
}

/** Plays notes one after another (single notes / riffs). */
export function arpeggio(midis: number[], when: number, step: number, instrument: Instrument) {
  midis.forEach((m, i) => {
    const buf = instrument === "piano" ? pianoBuffer(m) : pluckBuffer(m, instrument);
    playBuffer(buf, when + i * step, 0.75, when + (i + 1) * step + 0.4);
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

/** Pre-renders the buffers a song needs so the first strum isn't late. */
export function warmUp(midis: number[], instrument: Instrument) {
  for (const m of midis) (instrument === "piano" ? pianoBuffer : (x: number) => pluckBuffer(x, instrument))(m);
}
