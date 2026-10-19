// Radix-2 FFT for the audio analysis (no dependencies, no DOM: runs in a Worker and in Node).

/** In-place iterative radix-2 complex FFT of a fixed power-of-two size, with precomputed
 *  twiddles and bit-reversal table so each transform allocates nothing. */
export class FFT {
  readonly n: number;
  private readonly rev: Uint32Array;
  private readonly cos: Float64Array;
  private readonly sin: Float64Array;

  constructor(n: number) {
    if (n < 2 || (n & (n - 1)) !== 0) throw new Error(`FFT size must be a power of two, got ${n}`);
    this.n = n;
    const bits = Math.round(Math.log2(n));
    this.rev = new Uint32Array(n);
    for (let i = 0; i < n; i++) {
      let r = 0;
      for (let b = 0; b < bits; b++) r |= ((i >> b) & 1) << (bits - 1 - b);
      this.rev[i] = r;
    }
    this.cos = new Float64Array(n / 2);
    this.sin = new Float64Array(n / 2);
    for (let i = 0; i < n / 2; i++) {
      this.cos[i] = Math.cos((2 * Math.PI * i) / n);
      this.sin[i] = Math.sin((2 * Math.PI * i) / n);
    }
  }

  /** Forward transform X[k] = sum x[j] e^{-2 pi i jk/n}, in place. */
  transform(re: Float64Array, im: Float64Array): void {
    const n = this.n;
    const rev = this.rev;
    for (let i = 0; i < n; i++) {
      const j = rev[i];
      if (j > i) {
        let t = re[i]; re[i] = re[j]; re[j] = t;
        t = im[i]; im[i] = im[j]; im[j] = t;
      }
    }
    const cos = this.cos;
    const sin = this.sin;
    for (let size = 2; size <= n; size <<= 1) {
      const half = size >> 1;
      const step = n / size;
      for (let start = 0; start < n; start += size) {
        for (let k = 0, t = 0; k < half; k++, t += step) {
          const c = cos[t];
          const s = sin[t];
          const a = start + k;
          const b = a + half;
          // (re + i im) * (c - i s)
          const tr = re[b] * c + im[b] * s;
          const ti = im[b] * c - re[b] * s;
          re[b] = re[a] - tr;
          im[b] = im[a] - ti;
          re[a] += tr;
          im[a] += ti;
        }
      }
    }
  }
}

/** FFT of a real signal of length n via one complex FFT of length n/2 (the even samples as the
 *  real part, the odd ones as the imaginary part), then the usual split. Twice as fast as a
 *  complex FFT of the zero-imaginary signal, which matters for thousands of STFT frames. */
export class RealFFT {
  readonly n: number;
  private readonly half: FFT;
  private readonly zr: Float64Array;
  private readonly zi: Float64Array;
  private readonly cos: Float64Array;
  private readonly sin: Float64Array;

  constructor(n: number) {
    if (n < 4 || (n & (n - 1)) !== 0) throw new Error(`RealFFT size must be a power of two >= 4, got ${n}`);
    this.n = n;
    this.half = new FFT(n / 2);
    this.zr = new Float64Array(n / 2);
    this.zi = new Float64Array(n / 2);
    this.cos = new Float64Array(n / 2 + 1);
    this.sin = new Float64Array(n / 2 + 1);
    for (let k = 0; k <= n / 2; k++) {
      this.cos[k] = Math.cos((2 * Math.PI * k) / n);
      this.sin[k] = Math.sin((2 * Math.PI * k) / n);
    }
  }

  /** Spectrum bins 0..n/2 of the real input (length n). `outIm` may be omitted when only
   *  magnitudes are wanted: then `outRe` receives |X[k]|. */
  forward(input: ArrayLike<number>, outRe: Float64Array, outIm?: Float64Array): void {
    const m = this.n / 2;
    const zr = this.zr;
    const zi = this.zi;
    for (let k = 0; k < m; k++) {
      zr[k] = input[2 * k];
      zi[k] = input[2 * k + 1];
    }
    this.half.transform(zr, zi);
    const cos = this.cos;
    const sin = this.sin;
    for (let k = 0; k <= m; k++) {
      const k1 = k === m ? 0 : k;
      const k2 = k === 0 ? 0 : m - k;
      const a = zr[k1], b = zi[k1], c = zr[k2], d = zi[k2];
      // even / odd sample spectra
      const feR = (a + c) * 0.5, feI = (b - d) * 0.5;
      const foR = (b + d) * 0.5, foI = (c - a) * 0.5;
      const cs = cos[k], sn = sin[k];
      const xr = feR + cs * foR + sn * foI;
      const xi = feI + cs * foI - sn * foR;
      if (outIm) {
        outRe[k] = xr;
        outIm[k] = xi;
      } else {
        outRe[k] = Math.sqrt(xr * xr + xi * xi);
      }
    }
  }
}

/** Periodic Hann window (sums to a constant at 50 % / 75 % overlap). */
export function hann(n: number): Float64Array {
  const w = new Float64Array(n);
  for (let i = 0; i < n; i++) w[i] = 0.5 - 0.5 * Math.cos((2 * Math.PI * i) / n);
  return w;
}
