// Tuned semitone spectrogram and beat-synchronous chroma.

import { RealFFT, hann } from "./fft";
import { percentile } from "./dsp";

/** MIDI range of the semitone spectrogram: E1 (41 Hz) .. C7 (2093 Hz). */
export const PITCH_LO = 28;
export const PITCH_HI = 96;
export const PITCHES = PITCH_HI - PITCH_LO + 1;

// 0.37 s frames: resolves semitones down to ~80 Hz (and the bass roughly below that), the usual
// chord-recognition trade-off (Chordino uses the same window). Hop 46 ms ~ 10 frames per beat.
const N = 4096;
const HOP = 512;

export interface SemitoneSpectrogram {
  /** log-compressed semitone magnitudes, frames x PITCHES (row-major) */
  logSpec: Float32Array;
  frames: number;
  /** seconds per frame step; frame f is centred at f * hop seconds */
  hop: number;
  /** frame RMS (before log compression), for silence detection */
  rms: Float32Array;
  /** deviation of the recording's tuning from A = 440 Hz, in cents (-50..50) */
  tuningCents: number;
}

/** Tuning from the strongest spectral peaks of up to ~400 frames: the weighted circular mean of
 *  their offsets from the equal-tempered A440 grid (offsets live on a 100-cent circle). */
function estimateTuning(x: Float32Array, sr: number, fft: RealFFT, win: Float64Array, frames: number): number {
  const frame = new Float64Array(N);
  const mag = new Float64Array(N / 2 + 1);
  const binHz = sr / N;
  const kLo = Math.ceil(80 / binHz);
  const kHi = Math.min(N / 2 - 2, Math.floor(2000 / binHz));
  const step = Math.max(1, Math.floor(frames / 400));
  let sc = 0;
  let ss = 0;
  for (let t = 0; t < frames; t += step) {
    fillFrame(x, t * HOP - N / 2, win, frame);
    fft.forward(frame, mag);
    let mx = 0;
    for (let k = kLo; k <= kHi; k++) if (mag[k] > mx) mx = mag[k];
    if (mx <= 1e-9) continue;
    for (let k = Math.max(kLo, 2); k <= kHi; k++) {
      const m = mag[k];
      if (m < 0.1 * mx || m <= mag[k - 1] || m < mag[k + 1] || m <= mag[k - 2] || m < mag[k + 2]) continue;
      // parabolic interpolation on log magnitude: accurate to a few cents for a Hann window
      const a = Math.log(mag[k - 1] + 1e-12), b = Math.log(m), c = Math.log(mag[k + 1] + 1e-12);
      const den = a - 2 * b + c;
      const d = den < 0 ? (0.5 * (a - c)) / den : 0;
      const f = (k + d) * binHz;
      const cents = 1200 * Math.log2(f / 440);
      const dev = cents - 100 * Math.round(cents / 100);
      const th = (2 * Math.PI * dev) / 100;
      const w = m / mx;
      sc += w * Math.cos(th);
      ss += w * Math.sin(th);
    }
  }
  if (sc === 0 && ss === 0) return 0;
  return (Math.atan2(ss, sc) * 100) / (2 * Math.PI);
}

function fillFrame(x: Float32Array, start: number, win: Float64Array, out: Float64Array): void {
  for (let i = 0; i < N; i++) {
    const j = start + i;
    out[i] = j >= 0 && j < x.length ? x[j] * win[i] : 0;
  }
}

/**
 * Semitone spectrogram on the tuned grid. Each semitone takes the largest magnitude within
 * +-35 cents of its centre (or the interpolated value at the centre where FFT bins are wider than
 * that, in the bass): a sinusoid then reads the same at any pitch, while noise isn't summed over
 * the wide treble bands. Magnitudes are log-compressed relative to the song's loud level
 * (log(1 + 30 a)): ~40 dB of range, so quiet chords still count but weak noise doesn't.
 */
export function semitoneSpectrogram(x: Float32Array, sr: number, onProgress?: (f: number) => void): SemitoneSpectrogram {
  const frames = Math.floor(x.length / HOP) + 1;
  const fft = new RealFFT(N);
  const win = hann(N);
  const tuningCents = estimateTuning(x, sr, fft, win, frames);

  // pitch -> (bins to take the max over, and the interpolated centre bin)
  const binHz = sr / N;
  const lo = new Int32Array(PITCHES);
  const hi = new Int32Array(PITCHES);
  const centre = new Float64Array(PITCHES);
  for (let p = 0; p < PITCHES; p++) {
    const f = 440 * Math.pow(2, (PITCH_LO + p - 69 + tuningCents / 100) / 12);
    centre[p] = f / binHz;
    lo[p] = Math.ceil((f * Math.pow(2, -0.35 / 12)) / binHz);
    hi[p] = Math.floor((f * Math.pow(2, 0.35 / 12)) / binHz);
  }

  // the song's loud level: RMS of the louder blocks (silence and fades don't lower it)
  const blk = 2048;
  const blockRms: number[] = [];
  for (let i = 0; i + blk <= x.length; i += blk) {
    let s = 0;
    for (let j = i; j < i + blk; j++) s += x[j] * x[j];
    blockRms.push(Math.sqrt(s / blk));
  }
  const loud = Math.max(1e-4, percentile(blockRms, 0.9));
  // A Hann-windowed sinusoid of amplitude A peaks at A * N / 4.
  const gain = 30 / ((N / 4) * loud);

  const logSpec = new Float32Array(frames * PITCHES);
  const rms = new Float32Array(frames);
  const frame = new Float64Array(N);
  const mag = new Float64Array(N / 2 + 1);
  for (let t = 0; t < frames; t++) {
    fillFrame(x, t * HOP - N / 2, win, frame);
    let e = 0;
    for (let i = 0; i < N; i++) e += frame[i] * frame[i];
    rms[t] = Math.sqrt(e / (N * 0.375)); // Hann power gain is 3/8
    fft.forward(frame, mag);
    const row = t * PITCHES;
    for (let p = 0; p < PITCHES; p++) {
      const c = centre[p];
      const k = Math.floor(c);
      let v = mag[k] + (mag[k + 1] - mag[k]) * (c - k);
      for (let b = lo[p]; b <= hi[p]; b++) if (mag[b] > v) v = mag[b];
      logSpec[row + p] = Math.log1p(gain * v);
    }
    if (onProgress && (t & 255) === 0) onProgress(t / frames);
  }
  return { logSpec, frames, hop: HOP / sr, rms, tuningCents };
}

export interface BeatChroma {
  /** treble chroma per beat (12 values, pitch class 0 = C) */
  treble: Float64Array[];
  /** bass chroma per beat */
  bass: Float64Array[];
  /** whitened tonal energy per beat (sum of the treble chroma) */
  tonal: Float64Array;
  /** mean frame RMS per beat */
  level: Float64Array;
}

// Pitch weightings. Treble: A2..F#6, full weight E3..E5: chord voicings live there, and above
// ~E5 most energy is upper harmonics of lower notes (3rd harmonics add fifths, 5th harmonics
// thirds), which would blur the chord. Bass: E1..F#3, full weight A1..C3.
function ramp(p: number, a: number, b: number, c: number, d: number): number {
  if (p <= a || p >= d) return 0;
  if (p < b) return (p - a) / (b - a);
  if (p > c) return (d - p) / (d - c);
  return 1;
}
const TREBLE_W = Array.from({ length: PITCHES }, (_, i) => ramp(PITCH_LO + i, 44, 52, 76, 91));
const BASS_W = Array.from({ length: PITCHES }, (_, i) => ramp(PITCH_LO + i, 27, 33, 48, 55));

/**
 * Beat-synchronous chroma: each beat averages the log semitone spectrum of the frames centred in
 * it, weighted by a sine^2 bump over the beat (the frames near the beat edges overlap the previous
 * and next chord), then whitens it: the running mean over +-6 semitones is subtracted and
 * negatives dropped, which removes the spectral envelope and broadband drum energy and keeps the
 * notes that stand out from their neighbourhood. Averaging before whitening keeps noise from
 * adding a positive bias.
 */
export function beatChroma(spec: SemitoneSpectrogram, beatTimes: number[]): BeatChroma {
  const n = beatTimes.length;
  const treble: Float64Array[] = [];
  const bass: Float64Array[] = [];
  const tonal = new Float64Array(n);
  const level = new Float64Array(n);
  const avg = new Float64Array(PITCHES);
  const white = new Float64Array(PITCHES);
  for (let i = 0; i < n; i++) {
    const a = beatTimes[i];
    const b = i + 1 < n ? beatTimes[i + 1] : a + (n > 1 ? beatTimes[n - 1] - beatTimes[n - 2] : 0.5);
    avg.fill(0);
    let wsum = 0;
    let lsum = 0;
    let f0 = Math.max(0, Math.ceil(a / spec.hop));
    let f1 = Math.min(spec.frames - 1, Math.floor(b / spec.hop));
    if (f1 < f0) {
      // a beat shorter than a frame step: use the nearest frame
      f0 = f1 = Math.min(spec.frames - 1, Math.max(0, Math.round((0.5 * (a + b)) / spec.hop)));
    }
    for (let f = f0; f <= f1; f++) {
      const ph = (f * spec.hop - a) / Math.max(1e-6, b - a);
      const w = 0.05 + Math.sin(Math.PI * Math.min(1, Math.max(0, ph))) ** 2;
      const row = f * PITCHES;
      for (let p = 0; p < PITCHES; p++) avg[p] += w * spec.logSpec[row + p];
      lsum += w * spec.rms[f];
      wsum += w;
    }
    for (let p = 0; p < PITCHES; p++) avg[p] /= wsum;
    level[i] = lsum / wsum;
    for (let p = 0; p < PITCHES; p++) {
      let s = 0;
      let c = 0;
      for (let q = Math.max(0, p - 6); q <= Math.min(PITCHES - 1, p + 6); q++) {
        s += avg[q];
        c++;
      }
      const v = avg[p] - s / c;
      white[p] = v > 0 ? v : 0;
    }
    const tr = new Float64Array(12);
    const bs = new Float64Array(12);
    for (let p = 0; p < PITCHES; p++) {
      const pc = (PITCH_LO + p) % 12;
      tr[pc] += TREBLE_W[p] * white[p];
      bs[pc] += BASS_W[p] * white[p];
    }
    let t = 0;
    for (let k = 0; k < 12; k++) t += tr[k];
    treble.push(tr);
    bass.push(bs);
    tonal[i] = t;
  }
  return { treble, bass, tonal, level };
}

/**
 * How strongly the harmony repeats at a given lag (seconds), 0..1. Built from a chord-change
 * curve (how much the smoothed chroma differs 0.14 s before and after each frame), whose
 * autocorrelation peaks at the spacing of chord changes: usually the bar or the half-bar.
 */
export function harmonicPeriodicity(spec: SemitoneSpectrogram, maxSeconds = 12): (seconds: number) => number {
  const F = spec.frames;
  const chroma = new Float64Array(F * 12);
  for (let t = 0; t < F; t++) {
    const row = t * PITCHES;
    for (let p = 0; p < PITCHES; p++) chroma[t * 12 + ((PITCH_LO + p) % 12)] += spec.logSpec[row + p];
  }
  // smooth over +-2 frames, then scale each frame to unit length
  const sm = new Float64Array(F * 12);
  for (let t = 0; t < F; t++) {
    let norm = 0;
    for (let k = 0; k < 12; k++) {
      let v = 0;
      for (let d = -2; d <= 2; d++) {
        const u = Math.min(F - 1, Math.max(0, t + d));
        v += chroma[u * 12 + k];
      }
      sm[t * 12 + k] = v;
      norm += v * v;
    }
    norm = Math.sqrt(norm);
    for (let k = 0; k < 12; k++) sm[t * 12 + k] = norm > 1e-6 ? sm[t * 12 + k] / norm : 0;
  }
  const D = 3;
  const nov = new Float64Array(F);
  let mean = 0;
  for (let t = D; t + D < F; t++) {
    let dot = 0;
    for (let k = 0; k < 12; k++) dot += sm[(t + D) * 12 + k] * sm[(t - D) * 12 + k];
    nov[t] = Math.max(0, 1 - dot);
    mean += nov[t];
  }
  mean /= Math.max(1, F);
  for (let t = 0; t < F; t++) nov[t] -= mean;
  const maxLag = Math.min(F - 1, Math.ceil(maxSeconds / spec.hop) + 2);
  const r = new Float64Array(Math.max(1, maxLag + 1));
  for (let lag = 0; lag <= maxLag; lag++) {
    let acc = 0;
    for (let t = 0; t + lag < F; t++) acc += nov[t] * nov[t + lag];
    r[lag] = acc / (F - lag);
  }
  const r0 = r[0] > 0 ? r[0] : 1;
  const at = (x: number) => {
    const i = Math.floor(x);
    if (i < 0 || i + 1 >= r.length) return 0;
    return (r[i] + (r[i + 1] - r[i]) * (x - i)) / r0;
  };
  return (seconds: number) => {
    const x = seconds / spec.hop;
    return Math.max(0, at(x - 1), at(x - 0.5), at(x), at(x + 0.5), at(x + 1));
  };
}
