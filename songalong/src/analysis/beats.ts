// Tempo estimation and beat tracking (after Ellis 2007, "Beat tracking by dynamic programming").

export interface TempoEstimate {
  /** beat period in frames (fractional) */
  period: number;
  /** 0..1: how clearly the winning period stands out */
  confidence: number;
  /** the strongest rival periods (frames) with their scores, best first; [0] is the winner */
  candidates: { period: number; score: number }[];
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
 * autocorrelation plus half that at twice and four times the period (half-bars and bars repeat
 * too: Ellis's "TPS2" extended to the bar), weighted by a log-Gaussian prior around 115 BPM.
 * The bar term matters for accented strums: a 3+3+2 pattern repeats strongly every three 16ths,
 * a false pulse whose "bars" don't line up, while the true beat's do. Searching a fine BPM grid
 * with interpolated lags gives a fractional period directly.
 */
export function estimateTempo(env: Float64Array, fps: number): TempoEstimate {
  const maxLag = Math.ceil((fps * 60 * 4) / MIN_BPM) + 2;
  const r = autocorrelation(env, maxLag);
  const priorLag = (fps * 60) / PRIOR_BPM;
  let bestScore = -Infinity;
  let bestLag = priorLag;
  const scores: number[] = [];
  for (let bpm = MIN_BPM; bpm <= MAX_BPM; bpm += 0.1) {
    const lag = (fps * 60) / bpm;
    const oct = Math.log2(lag / priorLag) / PRIOR_OCTAVES;
    const w = Math.exp(-0.5 * oct * oct);
    const score = w * (peakNear(r, lag) + 0.5 * peakNear(r, 2 * lag) + 0.5 * peakNear(r, 4 * lag));
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
  // local maxima of the score over the BPM grid: the rival readings of the pulse
  const peaks: { period: number; score: number }[] = [];
  for (let k = 1; k + 1 < scores.length; k++) {
    if (scores[k] > scores[k - 1] && scores[k] >= scores[k + 1]) peaks.push({ period: (fps * 60) / (MIN_BPM + 0.1 * k), score: scores[k] });
  }
  peaks.sort((a, b) => b.score - a.score);
  // one entry per reading: drop peaks within 3 % of a stronger one
  const candidates = [{ period: lag, score: bestScore }];
  for (const p of peaks) {
    if (candidates.length >= 8) break;
    if (candidates.every((c) => Math.abs(p.period / c.period - 1) > 0.03)) candidates.push(p);
  }
  return { period: lag, confidence, candidates };
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

/** a slide moves the beat this far (share of a beat), in at most MAX_SLIDE beats */
const SLIDE_MIN = 0.3;
const SLIDE_MAX = 0.7;
const MAX_SLIDE = 4;
/** an interval this far off the local beat counts as part of a slide; within it, as steady */
const OFF = 0.06;
/** steady beats needed on both sides of a slide */
const STEADY = 3;

/**
 * Puts back beats that slid off the beat for a while. When the off-beat strums get louder for
 * some bars, a tracker can slide onto them: a few beats come 10-20% short (or long), the next
 * ones sit half a beat off, until it slides back, or to the end. Music doesn't change phase:
 * every stretch between slides is moved by what the slides moved it, so that it agrees with
 * the most of the song. A tempo change is no slide: the beats after it don't return to the
 * pulse before it. Times in seconds, in order; returns the same number of beats.
 */
export function mendSlips(beats: number[]): number[] {
  const n = beats.length;
  if (n < 2 * STEADY + MAX_SLIDE + 2) return beats;
  const ibi = beats.slice(1).map((b, i) => b - beats[i]);
  const steadyAround = (i: number, p: number) => Math.abs(ibi[i] - p) <= OFF * p;
  const slides: { from: number; to: number; shift: number }[] = []; // intervals from..to
  for (let i = STEADY; i < ibi.length - STEADY; ) {
    // the pulse just before
    const before = ibi.slice(i - STEADY, i);
    const p = [...before].sort((a, b) => a - b)[STEADY >> 1];
    let found: { from: number; to: number; shift: number } | null = null;
    if (before.every((_, k) => steadyAround(i - STEADY + k, p))) {
      for (let k = 1; k <= MAX_SLIDE && i + k + STEADY <= ibi.length; k++) {
        const devs = ibi.slice(i, i + k).map((d) => d - p);
        const shift = devs.reduce((a, b) => a + b, 0);
        const oneWay = devs.every((d) => Math.sign(d) === Math.sign(shift) && Math.abs(d) > OFF * p);
        const after = Array.from({ length: STEADY }, (_, j) => i + k + j).every((j) => steadyAround(j, p));
        if (oneWay && after && Math.abs(shift) >= SLIDE_MIN * p && Math.abs(shift) <= SLIDE_MAX * p) {
          found = { from: i, to: i + k - 1, shift };
          break;
        }
      }
    }
    if (found) {
      slides.push(found);
      i = found.to + 1 + STEADY;
    } else i++;
  }
  if (!slides.length) return beats;

  // stretches between slides (beat ranges) and how far each has slid from the first
  const stretches: { from: number; to: number; offset: number }[] = [];
  let start = 0;
  let offset = 0;
  for (const s of slides) {
    stretches.push({ from: start, to: s.from, offset });
    offset += s.shift;
    start = s.to + 1;
  }
  stretches.push({ from: start, to: n - 1, offset });
  // the offset most of the song's beats agree with (offsets within a quarter beat are one)
  const p = [...ibi].sort((a, b) => a - b)[ibi.length >> 1];
  let major = 0;
  let most = -1;
  for (const s of stretches) {
    const weight = stretches.filter((t) => Math.abs(t.offset - s.offset) < 0.25 * p).reduce((w, t) => w + t.to - t.from + 1, 0);
    if (weight > most) {
      most = weight;
      major = s.offset;
    }
  }
  const out = beats.slice();
  for (const s of stretches) {
    const off = major - s.offset;
    if (Math.abs(off) < 0.25 * p) continue; // already with the most of the song
    // it sits on the off-beat strums, so it moves by whole half-beats (the slides' own lengths
    // are only roughly that)
    const own = ibi.slice(s.from, Math.max(s.from + 1, s.to)).sort((a, b) => a - b);
    const half = (own[own.length >> 1] ?? p) / 2;
    const shift = Math.round(off / half) * half;
    for (let i = s.from; i <= s.to; i++) out[i] = beats[i] + shift;
  }
  // the beats inside a slide: evenly between the mended beats either side
  for (const s of slides) {
    const a = out[s.from];
    const b = out[s.to + 1];
    const k = s.to + 1 - s.from;
    for (let j = 1; j < k; j++) out[s.from + j] = a + ((b - a) * j) / k;
  }
  return out;
}
