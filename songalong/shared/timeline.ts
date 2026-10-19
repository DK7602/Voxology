// Beats <-> seconds, and "what is playing at beat b".

import type { ChordEvent, LyricLine, SongDoc } from "./types";

export class Timeline {
  readonly doc: SongDoc;
  private readonly beatTimes: number[] | null;
  private readonly spb: number;

  constructor(doc: SongDoc) {
    this.doc = doc;
    this.beatTimes = doc.beatTimes && doc.beatTimes.length >= 2 ? doc.beatTimes : null;
    this.spb = 60 / (doc.bpm || 100);
  }

  /** seconds (media time) of a beat position (fractional beats allowed) */
  beatToTime(beat: number): number {
    const bt = this.beatTimes;
    if (!bt) return this.doc.offset + beat * this.spb;
    const n = bt.length;
    if (beat <= 0) return bt[0] + beat * (bt[1] - bt[0]);
    if (beat >= n - 1) return bt[n - 1] + (beat - (n - 1)) * (bt[n - 1] - bt[n - 2]);
    const i = Math.floor(beat);
    return bt[i] + (beat - i) * (bt[i + 1] - bt[i]);
  }

  /** beat position (fractional) at media time `t` */
  timeToBeat(t: number): number {
    const bt = this.beatTimes;
    if (!bt) return (t - this.doc.offset) / this.spb;
    const n = bt.length;
    if (t <= bt[0]) return (t - bt[0]) / (bt[1] - bt[0]);
    if (t >= bt[n - 1]) return n - 1 + (t - bt[n - 1]) / (bt[n - 1] - bt[n - 2]);
    let lo = 0;
    let hi = n - 1;
    while (hi - lo > 1) {
      const mid = (lo + hi) >> 1;
      if (bt[mid] <= t) lo = mid;
      else hi = mid;
    }
    return lo + (t - bt[lo]) / (bt[lo + 1] - bt[lo]);
  }

  get lengthBeats(): number {
    const lastChord = this.doc.chords.at(-1)?.beat ?? 0;
    const lastLine = this.doc.lines.at(-1)?.beat ?? 0;
    return Math.max(this.doc.length || 0, lastChord + this.doc.beatsPerBar, lastLine + this.doc.beatsPerBar);
  }

  get duration(): number {
    return this.beatToTime(this.lengthBeats);
  }

  /** seconds per beat around beat b */
  secondsPerBeat(beat = 0): number {
    return this.beatToTime(beat + 1) - this.beatToTime(beat);
  }

  /** current tempo in BPM around beat b (measured beats may drift) */
  bpmAt(beat: number): number {
    return 60 / this.secondsPerBeat(Math.max(0, Math.floor(beat)));
  }
}

/** Index of the last item whose beat <= b (-1 if none). Items must be sorted by beat. */
export function indexAt<T extends { beat: number }>(items: readonly T[], b: number): number {
  let lo = 0;
  let hi = items.length - 1;
  let ans = -1;
  while (lo <= hi) {
    const mid = (lo + hi) >> 1;
    if (items[mid].beat <= b + 1e-9) {
      ans = mid;
      lo = mid + 1;
    } else hi = mid - 1;
  }
  return ans;
}

export function chordEnd(chords: readonly ChordEvent[], i: number, length: number): number {
  return i + 1 < chords.length ? chords[i + 1].beat : Math.max(length, chords[i].beat + 1);
}

export function lineEnd(lines: readonly LyricLine[], i: number, length: number): number {
  const next = i + 1 < lines.length ? lines[i + 1].beat : length;
  const end = lines[i].end;
  return end !== undefined && end < next ? end : next;
}

export interface PlacedChord {
  chord: ChordEvent;
  index: number;
  col: number;
  /** started before the line (still sounding when it begins) */
  carried: boolean;
}

/** Chords over line i, with the column each sits at. A chord still sounding from before the
 *  line is included (carried) at column 0 when no chord starts with the line. Columns not
 *  stored are placed in proportion to time across the text, snapped to the start of a word. */
export function chordsInLine(doc: SongDoc, i: number): PlacedChord[] {
  const line = doc.lines[i];
  const start = line.beat;
  const end = i + 1 < doc.lines.length ? doc.lines[i + 1].beat : Math.max(doc.length, start + doc.beatsPerBar);
  const textEnd = lineEnd(doc.lines, i, end);
  const out: PlacedChord[] = [];
  const eps = 1e-6;
  const sounding = indexAt(doc.chords, start + eps);
  if (sounding >= 0 && doc.chords[sounding].beat < start - eps) {
    out.push({ chord: doc.chords[sounding], index: sounding, col: 0, carried: true });
  }
  for (let c = Math.max(0, sounding); c < doc.chords.length; c++) {
    const ch = doc.chords[c];
    if (ch.beat < start - eps) continue;
    if (ch.beat >= end - eps) break;
    const col =
      ch.col !== undefined
        ? Math.min(ch.col, line.text.length)
        : snapToWord(line.text, Math.round(((ch.beat - start) / Math.max(textEnd - start, eps)) * line.text.length));
    out.push({ chord: ch, index: c, col: Math.max(0, col), carried: false });
  }
  return out;
}

function snapToWord(text: string, col: number): number {
  if (col <= 0 || !text) return 0;
  if (col >= text.length) return text.length;
  let i = col;
  while (i > 0 && text[i - 1] !== " ") i--;
  return i;
}

export interface Bar {
  index: number;
  start: number; // beat
  /** chord changes inside this bar: [beat offset in bar, chord index] */
  changes: { at: number; chordIndex: number }[];
}

/** The song split into bars, each listing where chords change. */
export function bars(doc: SongDoc, lengthBeats: number): Bar[] {
  const out: Bar[] = [];
  const bpb = doc.beatsPerBar || 4;
  const count = Math.ceil(lengthBeats / bpb);
  let c = 0;
  for (let b = 0; b < count; b++) {
    const start = b * bpb;
    const end = start + bpb;
    const changes: Bar["changes"] = [];
    const sounding = indexAt(doc.chords, start);
    if (sounding >= 0) changes.push({ at: 0, chordIndex: sounding });
    while (c < doc.chords.length && doc.chords[c].beat < end) {
      const at = doc.chords[c].beat - start;
      if (at > 1e-9) changes.push({ at, chordIndex: c });
      c++;
    }
    out.push({ index: b, start, changes });
  }
  return out;
}
