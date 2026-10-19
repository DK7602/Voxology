// Small signal helpers shared by the analysis stages.

/** One RBJ-cookbook biquad section (transposed direct form II). */
class Biquad {
  private z1 = 0;
  private z2 = 0;
  constructor(
    private readonly b0: number,
    private readonly b1: number,
    private readonly b2: number,
    private readonly a1: number,
    private readonly a2: number,
  ) {}

  static lowpass(fc: number, fs: number, q: number): Biquad {
    const w0 = (2 * Math.PI * fc) / fs;
    const cw = Math.cos(w0);
    const alpha = Math.sin(w0) / (2 * q);
    const a0 = 1 + alpha;
    return new Biquad((1 - cw) / 2 / a0, (1 - cw) / a0, (1 - cw) / 2 / a0, (-2 * cw) / a0, (1 - alpha) / a0);
  }

  run(x: Float32Array): void {
    const { b0, b1, b2, a1, a2 } = this;
    let z1 = this.z1, z2 = this.z2;
    for (let i = 0; i < x.length; i++) {
      const v = x[i];
      const y = b0 * v + z1;
      z1 = b1 * v - a1 * y + z2;
      z2 = b2 * v - a2 * y;
      x[i] = y;
    }
    this.z1 = z1;
    this.z2 = z2;
  }
}

/** Resamples to `target` Hz. Downsampling first runs an 8th-order Butterworth low-pass at 45 % of
 *  the new rate (aliases folding into the chroma range, under 2 kHz, come from 9 kHz and up and are
 *  > 60 dB down), then reads the filtered signal at the new rate by linear interpolation. The
 *  filter's phase delay (a fraction of a millisecond) is far below the beat-timing tolerance. */
export function resample(samples: Float32Array, sampleRate: number, target: number): Float32Array {
  if (Math.abs(sampleRate - target) < 1e-6) return Float32Array.from(samples);
  let src = samples;
  if (sampleRate > target) {
    src = Float32Array.from(samples);
    const order = 8;
    for (let k = 1; k <= order / 2; k++) {
      const q = 1 / (2 * Math.sin(((2 * k - 1) * Math.PI) / (2 * order)));
      Biquad.lowpass(0.45 * target, sampleRate, q).run(src);
    }
  }
  const ratio = sampleRate / target;
  const n = Math.floor((src.length - 1) / ratio) + 1;
  const out = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    const pos = i * ratio;
    const j = Math.floor(pos);
    const f = pos - j;
    const a = src[j];
    const b = j + 1 < src.length ? src[j + 1] : a;
    out[i] = a + (b - a) * f;
  }
  return out;
}

export function median(values: ArrayLike<number>): number {
  const n = values.length;
  if (n === 0) return 0;
  const a = Float64Array.from(values as ArrayLike<number>).sort();
  return n % 2 ? a[(n - 1) / 2] : 0.5 * (a[n / 2 - 1] + a[n / 2]);
}

export function percentile(values: ArrayLike<number>, p: number): number {
  const n = values.length;
  if (n === 0) return 0;
  const a = Float64Array.from(values as ArrayLike<number>).sort();
  const pos = Math.min(n - 1, Math.max(0, p * (n - 1)));
  const j = Math.floor(pos);
  const f = pos - j;
  return j + 1 < n ? a[j] * (1 - f) + a[j + 1] * f : a[j];
}

/** Centred moving average with a window of 2*half+1 (shrinking at the edges). */
export function movingAverage(x: ArrayLike<number>, half: number): Float64Array {
  const n = x.length;
  const out = new Float64Array(n);
  const prefix = new Float64Array(n + 1);
  for (let i = 0; i < n; i++) prefix[i + 1] = prefix[i] + x[i];
  for (let i = 0; i < n; i++) {
    const a = Math.max(0, i - half);
    const b = Math.min(n, i + half + 1);
    out[i] = (prefix[b] - prefix[a]) / (b - a);
  }
  return out;
}

export const clamp = (v: number, lo: number, hi: number) => (v < lo ? lo : v > hi ? hi : v);
