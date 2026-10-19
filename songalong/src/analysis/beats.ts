// Tempo estimation and beat tracking (after Ellis 2007, "Beat tracking by dynamic programming").

export interface TempoEstimate {
  /** beat period in frames (fractional) */
  period: number;
  /** 0..1: how clearly the winning period stands out */
  confidence: number;
}

const PRIOR_BPM = 115; // log-Gaussian tempo prior centre (most pop / rock sits around 100-130)
const PRIOR_OCTAVES = 1.0; // its width
const MIN_BPM = 50;
const MAX_BPM = 220;

/** Autocorrelation of the onset envelope for lags 0..maxLag, normalised per overlap length. */
function autocorrelation(env: Float64Array, maxLag: number): Float64Array {
  const n = env.length;
  const r = new Float64Array(maxLag + 2);
  for (let lag = 0; lag <= maxLag + 1 && lag < n; lag++) {
    let s = 0;
    for (let i = 0; i + lag < n; i++) s += env[i] * env[i + lag];
    r[lag] = s / (n - lag);
  }
  return r;
}

function interp(r: Float64Array, x: number): number {
  if (x < 0) return 0;
  const i = Math.floor(x);
  if (i + 1 >= r.length) return i < r.length ? r[i] : 0;
  const f = x - i;
  return r[i] * (1 - f) + r[i + 1] * f;
}

/** Peak of the autocorrelation near `lag` (it can sit a frame off a fractional lag). */
function peakNear(r: Float64Array, lag: number): number {
  let best = 0;
  for (let d = -1; d <= 1; d += 0.5) best = Math.max(best, interp(r, lag + d));
  return best;
}

/**
 * Global tempo from the onset autocorrelation. Each candidate period is scored by its own
 * autocorrelation plus half that at twice the period (bars and half-bars repeat too: this is
 * Ellis's "TPS2", which favours the beat level over the tatum), weighted by a log-Gaussian
 * prior around 115 BPM. Searching a fine BPM grid with interpolated lags gives a fractional
 * period directly.
 */
export function estimateTempo(env: Float64Array, fps: number): TempoEstimate {
  const maxLag = Math.ceil((fps * 60 * 2) / MIN_BPM) + 2;
  const r = autocorrelation(env, maxLag);
  const priorLag = (fps * 60) / PRIOR_BPM;
  let bestScore = -Infinity;
  let bestLag = priorLag;
  const scores: number[] = [];
  for (let bpm = MIN_BPM; bpm <= MAX_BPM; bpm += 0.1) {
    const lag = (fps * 60) / bpm;
    const oct = Math.log2(lag / priorLag) / PRIOR_OCTAVES;
    const w = Math.exp(-0.5 * oct * oct);
    const score = w * (peakNear(r, lag) + 0.5 * peakNear(r, 2 * lag));
    scores.push(score);
    if (score > bestScore) {
      bestScore = score;
      bestLag = lag;
    }
  }
  // Refine the lag on the raw autocorrelation (parabola through the 3 frames around the peak).
  let lag = bestLag;
  const i = Math.round(lag);
  if (i > 1 && i + 1 < r.length) {
    let j = i;
    if (r[i - 1] > r[j]) j = i - 1;
    if (r[i + 1] > r[j]) j = i + 1;
    const a = r[j - 1], b = r[j], c = r[j + 1];
    const den = a - 2 * b + c;
    const off = den < 0 ? (0.5 * (a - c)) / den : 0;
    if (Math.abs(j + off - bestLag) < 1) lag = j + off;
  }
  const sorted = scores.slice().sort((a, b) => a - b);
  const med = sorted[Math.floor(sorted.length / 2)];
  const confidence = bestScore > 0 ? Math.max(0, Math.min(1, (bestScore - med) / bestScore)) : 0;
  return { period: lag, confidence };
}

/**
 * Dynamic-programming beat tracker. Every frame gets the best cumulative score of a beat
 * sequence ending there: its own (smoothed) onset strength plus the best predecessor 0.5..2
 * periods earlier, penalised by tightness * log(interval / period)^2. Backtracking from the
 * best late frame gives beats that sit on onsets but keep a steady pulse through gaps.
 * Returns fractional frame positions (sub-frame refined on the onset peaks).
 */
export function trackBeats(env: Float64Array, period: number, tightness = 100): number[] {
  const n = env.length;
  if (n === 0) return [];
  // Local score: the onset envelope smoothed by a narrow Gaussian (librosa's beat_local_score).
  const sigma = Math.max(0.5, period / 32);
  const half = Math.ceil(3 * sigma);
  const kernel = new Float64Array(2 * half + 1);
  for (let k = -half; k <= half; k++) kernel[k + half] = Math.exp(-0.5 * (k / sigma) ** 2);
  const local = new Float64Array(n);
  let localMax = 0;
  for (let i = 0; i < n; i++) {
    let s = 0;
    for (let k = -half; k <= half; k++) {
      const j = i + k;
      if (j >= 0 && j < n) s += env[j] * kernel[k + half];
    }
    local[i] = s;
    if (s > localMax) localMax = s;
  }

  const lo = Math.round(period / 2);
  const hi = Math.round(2 * period);
  const txwt = new Float64Array(hi + 1);
  for (let d = lo; d <= hi; d++) txwt[d] = -tightness * Math.log(d / period) ** 2;
  const cum = new Float64Array(n);
  const back = new Int32Array(n).fill(-1);
  let first = true;
  for (let i = 0; i < n; i++) {
    let best = -Infinity;
    let arg = -1;
    for (let d = lo; d <= hi; d++) {
      const j = i - d;
      // reaching back before the start costs only the transition weight
      const v = txwt[d] + (j >= 0 ? cum[j] : 0);
      if (v > best) {
        best = v;
        arg = j;
      }
    }
    cum[i] = local[i] + best;
    if (first && local[i] < 0.01 * localMax) back[i] = -1;
    else {
      back[i] = arg >= 0 ? arg : -1;
      first = false;
    }
  }

  // Last beat: the last local maximum of the cumulative score that is above half the median of them.
  const maxes: number[] = [];
  for (let i = 1; i < n - 1; i++) if (cum[i] > cum[i - 1] && cum[i] >= cum[i + 1]) maxes.push(i);
  let last = n - 1;
  if (maxes.length) {
    const vals = maxes.map((i) => cum[i]).sort((a, b) => a - b);
    const med = vals[Math.floor(vals.length / 2)];
    for (let k = maxes.length - 1; k >= 0; k--) {
      if (cum[maxes[k]] > 0.5 * med) {
        last = maxes[k];
        break;
      }
    }
  }
  const frames: number[] = [];
  for (let i = last; i >= 0; i = back[i]) {
    frames.push(i);
    if (back[i] < 0) break;
  }
  frames.reverse();

  // Trim weak beats at the ends (silence before / after the music), like librosa.
  let s2 = 0;
  for (const f of frames) s2 += local[f] * local[f];
  const thr = 0.5 * Math.sqrt(s2 / Math.max(1, frames.length));
  let a = 0;
  let b = frames.length;
  while (a < b && local[frames[a]] <= thr) a++;
  while (b > a && local[frames[b - 1]] <= thr) b--;
  const kept = frames.slice(a, b);

  // Sub-frame refinement: parabola through the onset peak when the beat sits on one.
  return kept.map((f) => {
    if (f <= 0 || f >= n - 1) return f;
    const p = local[f - 1], c = local[f], q = local[f + 1];
    if (c < p || c < q) return f;
    const den = p - 2 * c + q;
    if (den >= 0) return f;
    const off = (0.5 * (p - q)) / den;
    return f + Math.max(-0.5, Math.min(0.5, off));
  });
}
