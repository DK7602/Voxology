// The notes heard in a recording, by Spotify's Basic Pitch model (Apache-2.0, prep/models): it
// reads the audio in two-second windows and gives, per 11.6 ms frame and piano key, how likely a
// note is sounding and starting there. This is a port of its windowing and note-making
// (basic_pitch/inference.py and note_creation.py, version 0.4.0), with its default settings.

export const NOTES_RATE = 22050;
const HOP = 256;
const WINDOW = 2 * NOTES_RATE - HOP; // 43844 samples
const WINDOW_FRAMES = 172;
const OVERLAP_FRAMES = 30;
const OVERLAP = OVERLAP_FRAMES * HOP;
const STEP = WINDOW - OVERLAP;
const KEEP = WINDOW_FRAMES - OVERLAP_FRAMES; // frames kept from each window
const KEYS = 88;
const MIDI_OFFSET = 21; // key 0 = A0

const ONSET_THRESHOLD = 0.5;
const FRAME_THRESHOLD = 0.3;
const MIN_NOTE_FRAMES = 11; // 127.7 ms
const ENERGY_TOLERANCE = 11;

export interface Note {
  /** seconds */
  start: number;
  end: number;
  /** MIDI note number */
  pitch: number;
  /** mean likelihood while it sounds, 0..1 */
  amp: number;
}

/** The model on one window of WINDOW samples: likelihoods of a note sounding (`note`) and
 *  starting (`onset`), each WINDOW_FRAMES x 88, frame-major. */
export type NoteModel = (window: Float32Array) => Promise<{ note: Float32Array; onset: Float32Array }>;

/** Notes in mono audio at NOTES_RATE. */
export async function transcribe(audio: Float32Array, model: NoteModel, onProgress?: (fraction: number) => void): Promise<Note[]> {
  const padded = new Float32Array(OVERLAP / 2 + audio.length);
  padded.set(audio, OVERLAP / 2);
  const total = Math.floor(audio.length * (Math.floor(NOTES_RATE / HOP) / NOTES_RATE));
  const frames = new Float32Array(total * KEYS);
  const onsets = new Float32Array(total * KEYS);
  const windows = Math.ceil(padded.length / STEP);
  const buf = new Float32Array(WINDOW);
  for (let w = 0, out = 0; w < windows && out < total; w++) {
    buf.fill(0);
    buf.set(padded.subarray(w * STEP, w * STEP + WINDOW));
    const { note, onset } = await model(buf);
    // the middle of each window: half the overlap is dropped at each side
    const n = Math.min(KEEP, total - out);
    const from = (OVERLAP_FRAMES / 2) * KEYS;
    frames.set(note.subarray(from, from + n * KEYS), out * KEYS);
    onsets.set(onset.subarray(from, from + n * KEYS), out * KEYS);
    out += n;
    onProgress?.((w + 1) / windows);
  }
  return notesFrom(frames, onsets, total).map(([a, b, key, amp]) => ({ start: frameTime(a), end: frameTime(b), pitch: key + MIDI_OFFSET, amp }));
}

/** Seconds at a frame of the joined windows (each window contributes KEEP frames over STEP samples). */
function frameTime(f: number): number {
  return (Math.floor(f / KEEP) * STEP + (f % KEEP) * HOP) / NOTES_RATE;
}

/** Basic Pitch's output_to_notes_polyphonic: [startFrame, endFrame, key, amplitude] per note. */
export function notesFrom(frames: Float32Array, onsetsIn: Float32Array, n: number): [number, number, number, number][] {
  const onsets = inferOnsets(onsetsIn, frames, n);
  const remaining = Float32Array.from(frames);
  const at = (t: number, k: number) => t * KEYS + k;

  // onset peaks (local maxima in time) above the threshold, latest first
  const starts: [number, number][] = [];
  for (let t = 1; t + 1 < n; t++) {
    for (let k = 0; k < KEYS; k++) {
      const v = onsets[at(t, k)];
      if (v >= ONSET_THRESHOLD && v > onsets[at(t - 1, k)] && v > onsets[at(t + 1, k)]) starts.push([t, k]);
    }
  }
  starts.sort((p, q) => q[0] - p[0] || q[1] - p[1]);

  const clear = (from: number, to: number, k: number) => {
    for (let t = from; t < to; t++) {
      remaining[at(t, k)] = 0;
      if (k < KEYS - 1) remaining[at(t, k + 1)] = 0;
      if (k > 0) remaining[at(t, k - 1)] = 0;
    }
  };
  const mean = (from: number, to: number, k: number) => {
    let s = 0;
    for (let t = from; t < to; t++) s += frames[at(t, k)];
    return s / Math.max(1, to - from);
  };

  const notes: [number, number, number, number][] = [];
  for (const [start, k] of starts) {
    if (start >= n - 1) continue;
    // where the note fades below the frame threshold for good
    let i = start + 1;
    let quiet = 0;
    while (i < n - 1 && quiet < ENERGY_TOLERANCE) {
      quiet = remaining[at(i, k)] < FRAME_THRESHOLD ? quiet + 1 : 0;
      i++;
    }
    i -= quiet;
    if (i - start <= MIN_NOTE_FRAMES) continue;
    clear(start, i, k);
    notes.push([start, i, k, mean(start, i, k)]);
  }

  // the "melodia trick": notes still sounding without a clear onset, loudest first (the energy
  // left only ever drops to 0, so going down a sorted list is the same as taking the maximum anew)
  const loud: number[] = [];
  for (let j = 0; j < n * KEYS; j++) if (remaining[j] > FRAME_THRESHOLD) loud.push(j);
  loud.sort((p, q) => remaining[q] - remaining[p]);
  for (const j of loud) {
    if (remaining[j] <= FRAME_THRESHOLD) continue;
    const mid = Math.floor(j / KEYS);
    const k = j % KEYS;
    remaining[j] = 0;
    let i = mid + 1;
    let quiet = 0;
    while (i < n - 1 && quiet < ENERGY_TOLERANCE) {
      quiet = remaining[at(i, k)] < FRAME_THRESHOLD ? quiet + 1 : 0;
      clear(i, i + 1, k);
      i++;
    }
    const end = i - 1 - quiet;
    i = mid - 1;
    quiet = 0;
    while (i > 0 && quiet < ENERGY_TOLERANCE) {
      quiet = remaining[at(i, k)] < FRAME_THRESHOLD ? quiet + 1 : 0;
      clear(i, i + 1, k);
      i--;
    }
    const start = i + 1 + quiet;
    if (end - start <= MIN_NOTE_FRAMES) continue;
    notes.push([start, end, k, mean(start, end, k)]);
  }
  return notes.sort((p, q) => p[0] - q[0] || p[2] - q[2]);
}

/** Onsets also where a note's level jumps (Basic Pitch's get_infered_onsets, two frames back). */
function inferOnsets(onsets: Float32Array, frames: Float32Array, n: number): Float32Array {
  const diff = new Float32Array(n * KEYS);
  let maxDiff = 0;
  let maxOnset = 0;
  for (let j = 0; j < n * KEYS; j++) maxOnset = Math.max(maxOnset, onsets[j]);
  for (let t = 2; t < n; t++) {
    for (let k = 0; k < KEYS; k++) {
      const v = frames[t * KEYS + k];
      const d = Math.min(v - frames[(t - 1) * KEYS + k], v - frames[(t - 2) * KEYS + k]);
      if (d > 0) {
        diff[t * KEYS + k] = d;
        maxDiff = Math.max(maxDiff, d);
      }
    }
  }
  const out = new Float32Array(n * KEYS);
  const scale = maxDiff > 0 ? maxOnset / maxDiff : 0;
  for (let j = 0; j < n * KEYS; j++) out[j] = Math.max(onsets[j], diff[j] * scale);
  return out;
}
