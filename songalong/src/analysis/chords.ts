// Chord templates, per-beat chord scores and HMM (Viterbi) decoding with the bar position.

import type { BeatChroma } from "./chroma";
import { percentile } from "./dsp";

export interface ChordType {
  suffix: string;
  intervals: number[];
  /** score handicap (cosine units): richer chords must fit clearly better than a triad */
  penalty: number;
}

const TRIADS: ChordType[] = [
  { suffix: "", intervals: [0, 4, 7], penalty: 0 },
  { suffix: "m", intervals: [0, 3, 7], penalty: 0 },
];
const SEVENTHS: ChordType[] = [
  { suffix: "7", intervals: [0, 4, 7, 10], penalty: 0.05 },
  { suffix: "maj7", intervals: [0, 4, 7, 11], penalty: 0.05 },
  { suffix: "m7", intervals: [0, 3, 7, 10], penalty: 0.05 },
];

export class Vocabulary {
  readonly types: ChordType[];
  /** 12 roots x types, plus N.C. as the last state */
  readonly size: number;
  readonly nc: number;
  /** unit-length binary templates over the 12 pitch classes */
  readonly templates: Float64Array[];
  readonly roots: Int32Array;
  readonly typeOf: Int32Array;

  constructor(kind: "basic" | "full") {
    this.types = kind === "basic" ? TRIADS : [...TRIADS, ...SEVENTHS];
    this.nc = 12 * this.types.length;
    this.size = this.nc + 1;
    this.templates = [];
    this.roots = new Int32Array(this.size).fill(-1);
    this.typeOf = new Int32Array(this.size).fill(-1);
    for (let t = 0; t < this.types.length; t++) {
      for (let r = 0; r < 12; r++) {
        const k = t * 12 + r;
        const tpl = new Float64Array(12);
        for (const iv of this.types[t].intervals) tpl[(r + iv) % 12] = 1;
        const norm = Math.sqrt(this.types[t].intervals.length);
        for (let i = 0; i < 12; i++) tpl[i] /= norm;
        this.templates[k] = tpl;
        this.roots[k] = r;
        this.typeOf[k] = t;
      }
    }
  }

  pitchClasses(k: number): number[] {
    if (k === this.nc) return [];
    return this.types[this.typeOf[k]].intervals.map((iv) => (this.roots[k] + iv) % 12);
  }

  /** is it a major-type chord (major triad, 7, maj7)? */
  isMajor(k: number): boolean {
    if (k === this.nc) return false;
    return this.types[this.typeOf[k]].intervals.includes(4);
  }
}

const BASS_WEIGHT = 0.3; // how much the bass note votes for the root (cosine units)

/** A chord whose third is under this share of its root and fifth is a power chord. Measured on
 *  synthesized guitar, clean to heavily overdriven: power chords 0.08-0.32 (overdrive adds a
 *  faint "third"), full chords 0.51-0.97. */
const POWER_THIRD = 0.4;

/**
 * Which beats are power chords (root and fifth, no third: rock guitar's "E5"): each run of one
 * major or minor chord whose third, summed over the run, is weak next to its root and fifth. The
 * decoder can't tell those apart (with no third, E and Em fit the same), so this decides them.
 */
export function powerChords(ch: BeatChroma, states: number[], vocab: Vocabulary): boolean[] {
  const out = new Array<boolean>(states.length).fill(false);
  const sum = new Float64Array(12);
  for (let i = 0; i < states.length; ) {
    let j = i;
    while (j < states.length && states[j] === states[i]) j++;
    const s = states[i];
    if (s !== vocab.nc && vocab.types[vocab.typeOf[s]].intervals.length === 3) {
      const r = vocab.roots[s];
      sum.fill(0);
      for (let b = i; b < j; b++) for (let k = 0; k < 12; k++) sum[k] += ch.treble[b][k];
      const frame = (sum[r] + sum[(r + 7) % 12]) / 2;
      const third = Math.max(sum[(r + 3) % 12], sum[(r + 4) % 12]);
      if (frame > 0 && third < POWER_THIRD * frame) out.fill(true, i, j);
    }
    i = j;
  }
  return out;
}

/**
 * Emission scores per beat and state, in cosine units (higher = better):
 *   chord: cos(treble chroma, template) + 0.3 * (bass share on the root + 0.3 x on other chord
 *          tones) - type penalty [+ key bias]
 *   N.C.:  0.45, rising to ~1.3 as the beat's tonal energy falls towards silence / pure drums.
 */
export function emissionScores(ch: BeatChroma, vocab: Vocabulary, keyBias?: Float64Array): Float64Array[] {
  const n = ch.treble.length;
  const ref = Math.max(1e-9, percentile(ch.tonal, 0.75));
  const out: Float64Array[] = [];
  const unit = new Float64Array(12);
  const bshare = new Float64Array(12);
  for (let i = 0; i < n; i++) {
    const tr = ch.treble[i];
    const bs = ch.bass[i];
    let norm = 0;
    for (let k = 0; k < 12; k++) norm += tr[k] * tr[k];
    norm = Math.sqrt(norm) || 1;
    for (let k = 0; k < 12; k++) unit[k] = tr[k] / norm;
    let bsum = 0;
    for (let k = 0; k < 12; k++) bsum += bs[k];
    for (let k = 0; k < 12; k++) bshare[k] = bsum > 1e-9 ? bs[k] / bsum : 0;

    const s = new Float64Array(vocab.size);
    for (let k = 0; k < vocab.nc; k++) {
      const tpl = vocab.templates[k];
      let c = 0;
      for (let j = 0; j < 12; j++) c += unit[j] * tpl[j];
      const root = vocab.roots[k];
      let bassTerm = bshare[root];
      for (const pc of vocab.pitchClasses(k)) if (pc !== root) bassTerm += 0.3 * bshare[pc];
      s[k] = c + BASS_WEIGHT * bassTerm - vocab.types[vocab.typeOf[k]].penalty + (keyBias ? keyBias[k] : 0);
    }
    const rho = ch.tonal[i] / ref;
    s[vocab.nc] = 0.45 + 0.85 * Math.max(0, 1 - rho / 0.15);
    out.push(s);
  }
  return out;
}

/** Cost of a chord change landing on each beat of the bar (cosine units), for 4/4: changes are
 *  cheap on the downbeat, affordable on beat 3, dear on beats 2 and 4. */
function changeCosts(beatsPerBar: number): number[] {
  if (beatsPerBar === 4) return [0.12, 0.5, 0.25, 0.5];
  if (beatsPerBar === 3) return [0.12, 0.5, 0.45];
  return Array.from({ length: beatsPerBar }, (_, i) => (i === 0 ? 0.12 : 0.4));
}

export interface Decoded {
  states: number[];
  /** bar position of beat 0 (0 = beat 0 is a downbeat) */
  phase: number;
  score: number;
}

/**
 * Viterbi over chord states where the bar position advances one beat per beat, tried for each
 * phase of beat 0. Self-transitions are free; a change costs changeCosts[bar position], so the
 * phase whose downbeats line up with the chord changes wins (this is the downbeat estimate), and
 * the strong self-transition keeps one-beat flickers out. O(beats x states) per phase, using
 * the best and second-best predecessor.
 */
export function decodeChords(scores: Float64Array[], beatsPerBar: number): Decoded {
  const n = scores.length;
  if (n === 0) return { states: [], phase: 0, score: 0 };
  const K = scores[0].length;
  const cost = changeCosts(beatsPerBar);
  let best: Decoded = { states: [], phase: 0, score: -Infinity };
  const back = new Int32Array(n * K);
  let delta = new Float64Array(K);
  let next = new Float64Array(K);
  for (let phase = 0; phase < beatsPerBar; phase++) {
    delta.set(scores[0]);
    for (let i = 1; i < n; i++) {
      const pos = (phase + i) % beatsPerBar;
      const c = cost[pos];
      let b1 = -1, b2 = -1;
      for (let k = 0; k < K; k++) {
        if (b1 < 0 || delta[k] > delta[b1]) {
          b2 = b1;
          b1 = k;
        } else if (b2 < 0 || delta[k] > delta[b2]) b2 = k;
      }
      const e = scores[i];
      const row = i * K;
      for (let k = 0; k < K; k++) {
        const from = k === b1 ? b2 : b1;
        const change = delta[from] - c;
        if (delta[k] >= change) {
          next[k] = delta[k] + e[k];
          back[row + k] = k;
        } else {
          next[k] = change + e[k];
          back[row + k] = from;
        }
      }
      const t = delta;
      delta = next;
      next = t;
    }
    let end = 0;
    for (let k = 1; k < K; k++) if (delta[k] > delta[end]) end = k;
    if (delta[end] > best.score) {
      const states = new Array<number>(n);
      states[n - 1] = end;
      for (let i = n - 1; i > 0; i--) states[i - 1] = back[i * K + states[i]];
      best = { states, phase, score: delta[end] };
    }
  }
  return best;
}
