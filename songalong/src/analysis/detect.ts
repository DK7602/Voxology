// Automatic beat, downbeat, key and chord detection for an uploaded recording ("Chordify" mode).
// Pure TypeScript, no DOM: runs in a Web Worker (detect.worker.ts) and in Node tests.
//
// Pipeline:
//   1. resample to 11025 Hz (everything below 5 kHz is enough for beats and harmony)
//   2. onset envelope (log-band spectral flux) -> tempo (autocorrelation + tempo prior)
//      -> beats (Ellis dynamic programming), extended to cover the whole audible song
//   3. tuning estimate -> tuned log-semitone spectrogram -> beat-synchronous, whitened treble and
//      bass chroma
//   4. chord scores (templates + bass root) -> Viterbi with the bar position, which also places
//      the downbeat where chord changes land
//   5. key (profiles + tonic chord), a key-aware second chord pass, spelling in the key

import { DEFAULT_MAJOR, DEFAULT_MINOR, keyPrefersFlats, noteName, parseKey } from "../../shared/music/notes";
import { estimateTempo, trackBeats, type TempoEstimate } from "./beats";
import { beatChroma, harmonicPeriodicity, semitoneSpectrogram } from "./chroma";
import { Vocabulary, decodeChords, emissionScores } from "./chords";
import { clamp, median, resample } from "./dsp";
import { estimateKey, keyScale, type KeyEstimate } from "./key";
import { onsetEnvelope } from "./onset";

export interface DetectResult {
  /** rounded to 0.1 */
  bpm: number;
  beatsPerBar: number;
  /** seconds; beatTimes[0] is a downbeat (it may be negative when the song starts mid-bar) */
  beatTimes: number[];
  /** e.g. "G", "Ebm" */
  key: string;
  /** beat = index into beatTimes; consecutive duplicates merged; the first entry is at beat 0 */
  chords: { beat: number; name: string }[];
  /** beats (= beatTimes.length) */
  length: number;
  /** seconds of audio */
  duration: number;
  /** 0..1 rough overall confidence */
  confidence: number;
}

export interface DetectOptions {
  onProgress?: (fraction: number, stage: string) => void;
  /** "basic": major and minor triads; "full" (default): also 7, maj7 and m7 */
  vocabulary?: "basic" | "full";
}

const RATE = 11025;
const BEATS_PER_BAR = 4;

export function detectChords(samples: Float32Array, sampleRate: number, opts: DetectOptions = {}): DetectResult {
  const progress = (fraction: number, stage: string) => opts.onProgress?.(clamp(fraction, 0, 1), stage);
  const duration = samples.length / sampleRate;

  progress(0, "Preparing audio");
  const x = resample(samples, sampleRate, RATE);

  progress(0.08, "Finding the beat");
  const onset = onsetEnvelope(x, RATE);
  const tempo = estimateTempo(onset.env, onset.fps);
  const { start: audibleStart, end: audibleEnd } = audibleRange(onset.rms, onset.fps);
  if (audibleEnd <= audibleStart) return silentResult(duration);

  progress(0.2, "Listening to the harmony");
  const spec = semitoneSpectrogram(x, RATE, (f) => progress(0.2 + 0.6 * f, "Listening to the harmony"));
  const beatPeriodFrames = chooseByHarmony(tempo, onset.fps, harmonicPeriodicity(spec));
  // when the harmony overruled the rhythm, the loud accents sit off the beat: keep the grid steady
  const tightness = beatPeriodFrames === tempo.period ? 100 : 800;
  const tracked = trackBeats(onset.env, beatPeriodFrames, tightness).map((f) => f / onset.fps + onset.latency);
  const period = beatPeriodFrames / onset.fps;
  const beats = completeBeats(tracked, period, audibleStart, audibleEnd);
  const chroma = beatChroma(spec, beats);

  progress(0.88, "Naming the chords");
  const vocab = new Vocabulary(opts.vocabulary ?? "full");
  let decoded = decodeChords(emissionScores(chroma, vocab), BEATS_PER_BAR);
  let key = estimateKey(chroma, decoded.states, vocab);
  // Second pass: chords made of the key's notes get a small bonus, so ambiguous beats fall to
  // the diatonic reading (a Dm7 in C rather than an F6-ish F). The key is then re-read.
  const scores = emissionScores(chroma, vocab, keyBias(vocab, key));
  decoded = decodeChords(scores, BEATS_PER_BAR);
  key = estimateKey(chroma, decoded.states, vocab);

  progress(0.97, "Writing the chart");
  const keyName = (key.minor ? DEFAULT_MINOR : DEFAULT_MAJOR)[key.tonic] + (key.minor ? "m" : "");
  const flats = keyPrefersFlats(parseKey(keyName));
  const nameOf = (s: number) =>
    s === vocab.nc ? "N.C." : noteName(vocab.roots[s], flats) + vocab.types[vocab.typeOf[s]].suffix;

  // Make beat 0 a downbeat: drop leading no-chord beats before the first downbeat, or else
  // extrapolate the beats back to the bar start.
  let beatTimes = beats.slice();
  let states = decoded.states.slice();
  const lead = (BEATS_PER_BAR - decoded.phase) % BEATS_PER_BAR;
  if (lead > 0) {
    if (states.slice(0, lead).every((s) => s === vocab.nc) && beatTimes.length > lead) {
      beatTimes = beatTimes.slice(lead);
      states = states.slice(lead);
    } else {
      const p0 = beatTimes.length > 1 ? median(diffs(beatTimes.slice(0, 9))) : period;
      for (let j = 0; j < decoded.phase; j++) {
        beatTimes.unshift(beatTimes[0] - p0);
        states.unshift(vocab.nc);
      }
    }
  }

  const chords: { beat: number; name: string }[] = [];
  for (let i = 0; i < states.length; i++) {
    const name = nameOf(states[i]);
    if (!chords.length || chords[chords.length - 1].name !== name) chords.push({ beat: i, name });
  }
  if (!chords.length) chords.push({ beat: 0, name: "N.C." });

  const bpm = Math.round(10 * (60 / beatPeriod(tracked.length >= 8 ? tracked : beats, period))) / 10;
  const confidence = overallConfidence(tempo.confidence, scores, decoded.states, vocab, key);
  progress(1, "Done");
  return {
    bpm,
    beatsPerBar: BEATS_PER_BAR,
    beatTimes: beatTimes.map((t) => Math.round(t * 1000) / 1000),
    key: keyName,
    chords,
    length: beatTimes.length,
    duration,
    confidence,
  };
}

/**
 * The rhythm alone can mistake an accented strum for the beat: a pattern accented every three
 * 16ths reads as a pulse 4/3 as fast. Chords, though, change on bar lines. Among the rival
 * readings that aren't simply double or half the winner, take one whose bars (and half / double
 * bars) line up with where the harmony repeats when the winner's clearly don't.
 */
function chooseByHarmony(tempo: TempoEstimate, fps: number, harmony: (seconds: number) => number): number {
  const best = tempo.candidates[0];
  if (!best) return tempo.period;
  const fit = (frames: number) => {
    const p = frames / fps;
    return harmony(4 * p) + 0.5 * harmony(2 * p) + 0.5 * harmony(8 * p);
  };
  let choice = tempo.period;
  let choiceFit = fit(tempo.period);
  for (const c of tempo.candidates.slice(1)) {
    if (c.score < 0.45 * best.score) continue;
    const octaves = Math.log2(c.period / tempo.period);
    if (Math.abs(octaves - Math.round(octaves)) < 0.12) continue; // double / half: harmony can't tell
    const f = fit(c.period);
    if (f > choiceFit + 0.05 && f > 1.6 * choiceFit) {
      choice = c.period;
      choiceFit = f;
    }
  }
  return choice;
}

/** First and last moments the music is within 40 dB of its loudest frames. */
function audibleRange(rms: Float64Array, fps: number): { start: number; end: number } {
  let peak = 0;
  for (const v of rms) if (v > peak) peak = v;
  const thr = peak * 0.01;
  let a = 0;
  while (a < rms.length && rms[a] < thr) a++;
  let b = rms.length - 1;
  while (b > a && rms[b] < thr) b--;
  return { start: a / fps, end: b / fps };
}

function diffs(t: number[]): number[] {
  const d: number[] = [];
  for (let i = 1; i < t.length; i++) d.push(t[i] - t[i - 1]);
  return d;
}

/**
 * Extends the tracked beats at the local tempo so they cover all of the audible music: the
 * tracker drops weak beats at the ends (a soft intro, a ringing last chord), and those still need
 * chords. With too few tracked beats the grid is laid at the estimated tempo from the start.
 */
function completeBeats(tracked: number[], period: number, start: number, end: number): number[] {
  let beats = tracked.filter((t) => t > start - period && t < end + period);
  if (beats.length < 4) {
    beats = [];
    for (let t = start; t < end; t += period) beats.push(t);
    return beats.length ? beats : [start];
  }
  const p0 = median(diffs(beats.slice(0, 9)));
  while (beats[0] - p0 >= start - 0.25 * p0) beats.unshift(beats[0] - p0);
  const p1 = median(diffs(beats.slice(-9)));
  while (beats[beats.length - 1] + p1 < end) beats.push(beats[beats.length - 1] + p1);
  return beats;
}

/** Beat period from a least-squares line through the beat times (robust to small jitter). */
function beatPeriod(beats: number[], fallback: number): number {
  const n = beats.length;
  if (n < 2) return fallback;
  let sx = 0, sy = 0, sxx = 0, sxy = 0;
  for (let i = 0; i < n; i++) {
    sx += i;
    sy += beats[i];
    sxx += i * i;
    sxy += i * beats[i];
  }
  const slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);
  return slope > 0.2 && slope < 2 ? slope : fallback;
}

/** +0.03 for chords made only of the key's notes, -0.02 for the rest. */
function keyBias(vocab: Vocabulary, key: KeyEstimate): Float64Array {
  const scale = keyScale(key.tonic, key.minor);
  const bias = new Float64Array(vocab.size);
  for (let k = 0; k < vocab.nc; k++) bias[k] = vocab.pitchClasses(k).every((pc) => scale.has(pc)) ? 0.03 : -0.02;
  return bias;
}

function overallConfidence(tempoConf: number, scores: Float64Array[], states: number[], vocab: Vocabulary, key: KeyEstimate): number {
  let fit = 0;
  let n = 0;
  for (let i = 0; i < states.length; i++) {
    if (states[i] === vocab.nc) continue;
    const s = scores[i];
    let others = -Infinity;
    for (let k = 0; k < vocab.nc; k++) if (k !== states[i] && vocab.roots[k] !== vocab.roots[states[i]]) others = Math.max(others, s[k]);
    // margin of the chosen chord over the best chord on another root
    fit += clamp(0.5 + 3 * (s[states[i]] - others), 0, 1);
    n++;
  }
  const chordConf = n ? fit / n : 0;
  return Math.round(100 * clamp(0.3 * tempoConf + 0.5 * chordConf + 0.2 * key.confidence, 0, 1)) / 100;
}

function silentResult(duration: number): DetectResult {
  const beatTimes: number[] = [];
  for (let t = 0; t < Math.max(duration, 0.5); t += 0.5) beatTimes.push(Math.round(t * 1000) / 1000);
  return {
    bpm: 120,
    beatsPerBar: BEATS_PER_BAR,
    beatTimes,
    key: "C",
    chords: [{ beat: 0, name: "N.C." }],
    length: beatTimes.length,
    duration,
    confidence: 0,
  };
}
