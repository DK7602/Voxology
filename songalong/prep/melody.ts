// The lead part from the notes heard in a song: its top line (a riff, a solo, else the sung
// tune), put on the beat grid the way a lead part is written (shared/lead.ts): one note per
// 16th-note slot ("note:E4"), a quick run inside one slot ("notes:E4 G4"), rests ("N.C.").

import { midiNameInKey, type Key } from "../shared/music/notes";
import type { ChordEvent } from "../shared/types";
import type { Note } from "./notes";

/** The lead's range: E3 (under it, bass and the low strings of chords) to C6 (over it, mostly
 *  the overtones of what is sung or played: the model hears those as faint notes too). */
const LOW = 52;
const HIGH = 84;
/** quieter notes are left out (Basic Pitch's likelihood, 0.3 at the least) */
const MIN_AMP = 0.35;
/** notes starting this close together count as one moment */
const TOGETHER = 0.035;
/** the top note of two or more starting together is a chord's top string unless it stands at
 *  least this far above them (a tune sung or played over a strum) */
const CLEAR_ABOVE = 6;
/** the grid: 16th notes, but a note this close to an 8th goes there (a note picked again while
 *  it still rings is heard a little late) */
const SLOT = 0.25;
const EIGHTH_PULL = 0.15;
/** at most this many notes in one slot (a fast run), so the name stays short */
const MAX_RUN = 5;
/** Fractional beat at time t (straight lines between beats, extended at both ends). */
function beatAt(beatTimes: number[], t: number): number {
  const n = beatTimes.length;
  if (t <= beatTimes[0]) return (t - beatTimes[0]) / (beatTimes[1] - beatTimes[0]);
  if (t >= beatTimes[n - 1]) return n - 1 + (t - beatTimes[n - 1]) / (beatTimes[n - 1] - beatTimes[n - 2]);
  let lo = 0;
  let hi = n - 1;
  while (hi - lo > 1) {
    const mid = (lo + hi) >> 1;
    if (beatTimes[mid] <= t) lo = mid;
    else hi = mid;
  }
  return lo + (t - beatTimes[lo]) / (beatTimes[lo + 1] - beatTimes[lo]);
}

/** The notes starting within TOGETHER of each note (notes sorted by start). */
function together(sorted: Note[]): Note[][] {
  return sorted.map((n, i) => {
    const out: Note[] = [];
    for (let j = i - 1; j >= 0 && n.start - sorted[j].start <= TOGETHER; j--) out.push(sorted[j]);
    for (let j = i + 1; j < sorted.length && sorted[j].start - n.start <= TOGETHER; j++) out.push(sorted[j]);
    return out;
  });
}

/**
 * The notes that make the top line: nothing too low, high or faint, and at each moment the
 * highest note that starts then, unless it is the top string of a strummed chord.
 */
export function topLine(notes: Note[]): Note[] {
  const sorted = [...notes].sort((a, b) => a.start - b.start);
  const near = together(sorted);
  return sorted.filter((n, i) => {
    if (n.pitch < LOW || n.pitch > HIGH || n.amp < MIN_AMP) return false;
    const others = near[i].filter((o) => o.pitch <= HIGH);
    if (others.some((o) => o.pitch > n.pitch)) return false;
    const below = others.filter((o) => o.pitch < n.pitch);
    return below.length < 2 || n.pitch - Math.max(...below.map((o) => o.pitch)) >= CLEAR_ABOVE;
  });
}

/** A beat position on the grid: the nearest 8th when close to one, else the nearest 16th. */
function onGrid(beat: number): number {
  const eighth = Math.round(beat * 2) / 2;
  return Math.abs(beat - eighth) <= EIGHTH_PULL ? eighth : Math.round(beat / SLOT) * SLOT;
}

/** The lead part as events on the beat grid. */
export function leadPart(notes: Note[], beatTimes: number[], key: Key | null): ChordEvent[] {
  if (beatTimes.length < 2) return [];
  // a note heard twice in a row within a 16th is one note, lasting to the end of both
  const line: Note[] = [];
  for (const n of topLine(notes)) {
    const prev = line.at(-1);
    if (prev && prev.pitch === n.pitch && beatAt(beatTimes, n.start) - beatAt(beatTimes, prev.start) < SLOT) prev.end = Math.max(prev.end, n.end);
    else line.push({ ...n });
  }
  // notes by slot
  const slots = new Map<number, { notes: Note[]; end: number }>();
  for (const n of line) {
    const slot = onGrid(beatAt(beatTimes, n.start));
    if (slot < 0) continue;
    const end = Math.max(slot + SLOT, Math.round(beatAt(beatTimes, n.end) / SLOT) * SLOT);
    const s = slots.get(slot);
    if (s) {
      s.notes.push(n);
      s.end = Math.max(s.end, end);
    } else slots.set(slot, { notes: [n], end });
  }
  const events: ChordEvent[] = [];
  const order = [...slots.keys()].sort((a, b) => a - b);
  order.forEach((slot, i) => {
    const s = slots.get(slot)!;
    const run = s.notes.sort((a, b) => a.start - b.start);
    const kept = run.length <= MAX_RUN ? run : Array.from({ length: MAX_RUN }, (_, k) => run[Math.round((k * (run.length - 1)) / (MAX_RUN - 1))]);
    const names = kept.map((n) => midiNameInKey(n.pitch, key));
    events.push({ beat: slot, name: names.length === 1 ? `note:${names[0]}` : `notes:${names.join(" ")}` });
    // a rest when the note ends before the next one starts
    const next = order[i + 1];
    if (next === undefined || s.end < next) events.push({ beat: s.end, name: "N.C." });
  });
  return events;
}
