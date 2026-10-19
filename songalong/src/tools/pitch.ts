// Pitch detection for the tuner (no DOM: also used by the tests).

/** Pitch of a mono buffer by normalised autocorrelation (good for single plucked strings). */
export function detectPitch(buf: Float32Array, sampleRate: number, minHz = 50, maxHz = 1200): number | null {
  let rms = 0;
  for (let i = 0; i < buf.length; i++) rms += buf[i] * buf[i];
  rms = Math.sqrt(rms / buf.length);
  if (rms < 0.01) return null;
  const minLag = Math.floor(sampleRate / maxHz);
  const maxLag = Math.min(Math.floor(sampleRate / minHz), buf.length >> 1);
  const n = buf.length - maxLag;
  let bestLag = -1;
  let best = 0;
  const corr = new Float32Array(maxLag + 2);
  for (let lag = minLag; lag <= maxLag; lag++) {
    let c = 0;
    let e1 = 0;
    let e2 = 0;
    for (let i = 0; i < n; i++) {
      c += buf[i] * buf[i + lag];
      e1 += buf[i] * buf[i];
      e2 += buf[i + lag] * buf[i + lag];
    }
    corr[lag] = c / Math.sqrt(e1 * e2 + 1e-12);
  }
  // first strong peak (avoids octave-down errors)
  const top = Math.max(...corr.slice(minLag, maxLag + 1));
  for (let lag = minLag + 1; lag < maxLag; lag++) {
    if (corr[lag] > 0.9 * top && corr[lag] >= corr[lag - 1] && corr[lag] >= corr[lag + 1]) {
      bestLag = lag;
      best = corr[lag];
      break;
    }
  }
  if (bestLag < 0 || best < 0.6) return null;
  // parabolic interpolation
  const a = corr[bestLag - 1];
  const b = corr[bestLag];
  const c = corr[bestLag + 1];
  const shift = (a - c) / (2 * (a - 2 * b + c) || 1);
  return sampleRate / (bestLag + shift);
}
