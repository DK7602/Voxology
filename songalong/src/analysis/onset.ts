// Onset strength envelope: how much new sound starts in each ~12 ms frame.

import { RealFFT, hann } from "./fft";
import { movingAverage } from "./dsp";

export interface OnsetEnvelope {
  /** onset strength per frame, local mean removed, half-wave rectified, unit std */
  env: Float64Array;
  /** the same from the bands under 200 Hz only (kick drum, bass notes) */
  low: Float64Array;
  /** frame level (RMS of the windowed frame), for finding where the music starts and stops */
  rms: Float64Array;
  /** frames per second */
  fps: number;
  /** seconds of frame f: f / fps + latency (frames are centred on f * hop) */
  latency: number;
}

const N = 512; // 46 ms at 11025 Hz: short enough to place attacks precisely
const HOP = 128; // 11.6 ms frames
const BANDS = 40;
const F_LO = 30;
const F_HI = 5000;

/**
 * Log-band spectral flux (librosa / SuperFlux style): the spectrum is pooled into 40
 * log-spaced bands, compressed with log(1 + 100 * amplitude), and each band's rise over the
 * previous frame is counted (against the max of the neighbouring bands, so vibrato and slides
 * in a voice don't look like onsets). Drums and note attacks rise in many bands at once; the
 * band average makes them stand out from noise.
 */
export function onsetEnvelope(x: Float32Array, sr: number): OnsetEnvelope {
  const frames = Math.floor(x.length / HOP) + 1;
  const fft = new RealFFT(N);
  const win = hann(N);
  const frame = new Float64Array(N);
  const mag = new Float64Array(N / 2 + 1);

  // bin -> band (log spaced); bins outside [F_LO, F_HI] are ignored
  const bandOf = new Int32Array(N / 2 + 1).fill(-1);
  const binHz = sr / N;
  for (let k = 1; k <= N / 2; k++) {
    const f = k * binHz;
    if (f < F_LO || f > F_HI) continue;
    bandOf[k] = Math.min(BANDS - 1, Math.floor((BANDS * Math.log(f / F_LO)) / Math.log(F_HI / F_LO)));
  }
  const lowBands = Math.floor((BANDS * Math.log(200 / F_LO)) / Math.log(F_HI / F_LO));

  const band = new Float32Array(frames * BANDS);
  const rms = new Float64Array(frames);
  let maxBand = 1e-12;
  for (let t = 0; t < frames; t++) {
    const start = t * HOP - N / 2; // centred frames
    let e = 0;
    for (let i = 0; i < N; i++) {
      const j = start + i;
      const v = j >= 0 && j < x.length ? x[j] * win[i] : 0;
      frame[i] = v;
      e += v * v;
    }
    rms[t] = Math.sqrt(e / N);
    fft.forward(frame, mag);
    const row = t * BANDS;
    for (let k = 1; k <= N / 2; k++) {
      const b = bandOf[k];
      if (b >= 0) band[row + b] += mag[k] * mag[k];
    }
    for (let b = 0; b < BANDS; b++) {
      const a = Math.sqrt(band[row + b]);
      band[row + b] = a;
      if (a > maxBand) maxBand = a;
    }
  }
  // Log compression relative to the loudest band: ~40 dB of useful range. (A plain dB scale
  // with an 80 dB floor makes the first whisper of a note entering the window's tail count as a
  // big jump, which pulls onsets early.)
  const scale = 100 / maxBand;
  for (let i = 0; i < band.length; i++) band[i] = Math.log1p(scale * band[i]);

  const flux = new Float64Array(frames);
  const fluxLow = new Float64Array(frames);
  for (let t = 1; t < frames; t++) {
    const row = t * BANDS;
    const prev = row - BANDS;
    let s = 0;
    let sLow = 0;
    for (let b = 0; b < BANDS; b++) {
      let ref = band[prev + b];
      if (b > 0 && band[prev + b - 1] > ref) ref = band[prev + b - 1];
      if (b < BANDS - 1 && band[prev + b + 1] > ref) ref = band[prev + b + 1];
      const d = band[row + b] - ref;
      if (d > 0) {
        s += d;
        if (b < lowBands) sLow += d;
      }
    }
    flux[t] = s / BANDS;
    fluxLow[t] = sLow / Math.max(1, lowBands);
  }
  const fps = sr / HOP;
  return {
    env: normalise(flux, fps),
    low: normalise(fluxLow, fps),
    rms,
    fps,
    // A centred 46 ms window starts to see an attack before its centre, so the flux peaks a little
    // early; measured on synthetic drums and strummed chords, this puts beats on the attacks.
    latency: 0.003,
  };
}

/** Removes the slowly varying part (a ~1 s moving average), keeps the peaks, scales to unit std. */
function normalise(flux: Float64Array, fps: number): Float64Array {
  const mean = movingAverage(flux, Math.round(fps * 0.5));
  const out = new Float64Array(flux.length);
  let s2 = 0;
  for (let i = 0; i < flux.length; i++) {
    const v = flux[i] - mean[i];
    out[i] = v > 0 ? v : 0;
    s2 += out[i] * out[i];
  }
  const std = Math.sqrt(s2 / Math.max(1, flux.length)) || 1;
  for (let i = 0; i < out.length; i++) out[i] /= std;
  return out;
}
