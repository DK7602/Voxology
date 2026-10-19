// Key estimation: Krumhansl-Kessler profiles on the song's chroma, plus what the chords say.

import type { BeatChroma } from "./chroma";
import type { Vocabulary } from "./chords";

// Krumhansl & Kessler (1982) probe-tone profiles, tonic first.
const MAJOR = [6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88];
const MINOR = [6.33, 2.68, 3.52, 5.38, 2.6, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17];

export interface KeyEstimate {
  tonic: number;
  minor: boolean;
  /** 0..1, from the margin over the runner-up */
  confidence: number;
}

function pearson(a: ArrayLike<number>, b: ArrayLike<number>): number {
  const n = a.length;
  let ma = 0, mb = 0;
  for (let i = 0; i < n; i++) {
    ma += a[i];
    mb += b[i];
  }
  ma /= n;
  mb /= n;
  let sab = 0, saa = 0, sbb = 0;
  for (let i = 0; i < n; i++) {
    const x = a[i] - ma, y = b[i] - mb;
    sab += x * y;
    saa += x * x;
    sbb += y * y;
  }
  return saa > 0 && sbb > 0 ? sab / Math.sqrt(saa * sbb) : 0;
}

/**
 * Scores all 24 keys by
 *  - the correlation of the song's pitch-class profile (each chorded beat's treble chroma
 *    normalised to sum 1, plus half its bass chroma: the bass leans on the tonic) with the key
 *    profile, and
 *  - how much the decoded chords make the key's tonic chord "home": its share of the chorded beats
 *    and whether the song starts / ends on it. Profiles alone can't tell a relative major from its
 *    minor (Am F C G has the notes of C major); the chords that open, close and recur can.
 */
export function estimateKey(ch: BeatChroma, states: number[], vocab: Vocabulary): KeyEstimate {
  const prof = new Float64Array(12);
  for (let i = 0; i < ch.treble.length; i++) {
    if (states.length && states[i] === vocab.nc) continue;
    let t = 0, b = 0;
    for (let k = 0; k < 12; k++) {
      t += ch.treble[i][k];
      b += ch.bass[i][k];
    }
    for (let k = 0; k < 12; k++) {
      if (t > 0) prof[k] += ch.treble[i][k] / t;
      if (b > 0) prof[k] += (0.5 * ch.bass[i][k]) / b;
    }
  }
  const chorded = states.filter((s) => s !== vocab.nc);
  const first = chorded.length ? chorded[0] : -1;
  const last = chorded.length ? chorded[chorded.length - 1] : -1;

  const scores: { tonic: number; minor: boolean; score: number }[] = [];
  const rotated = new Float64Array(12);
  for (const minor of [false, true]) {
    const base = minor ? MINOR : MAJOR;
    for (let tonic = 0; tonic < 12; tonic++) {
      for (let k = 0; k < 12; k++) rotated[(tonic + k) % 12] = base[k];
      let score = pearson(prof, rotated);
      const isTonic = (s: number) => s >= 0 && s !== vocab.nc && vocab.roots[s] === tonic && vocab.isMajor(s) !== minor;
      if (chorded.length) {
        let share = 0;
        for (const s of chorded) if (isTonic(s)) share++;
        score += 0.25 * (share / chorded.length);
        if (isTonic(first)) score += 0.08;
        if (isTonic(last)) score += 0.08;
      }
      scores.push({ tonic, minor, score });
    }
  }
  scores.sort((a, b) => b.score - a.score);
  const margin = scores[0].score - scores[1].score;
  return { tonic: scores[0].tonic, minor: scores[0].minor, confidence: Math.max(0, Math.min(1, 0.4 + 4 * margin)) };
}

/** Pitch classes of the key's scale: major, or natural minor plus the raised 7th (harmonic minor's
 *  major V chord is as much "in" a minor key as the natural v). */
export function keyScale(tonic: number, minor: boolean): Set<number> {
  const steps = minor ? [0, 2, 3, 5, 7, 8, 10, 11] : [0, 2, 4, 5, 7, 9, 11];
  return new Set(steps.map((s) => (tonic + s) % 12));
}
