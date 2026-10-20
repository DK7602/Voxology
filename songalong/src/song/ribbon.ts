// The lyrics lane's rows (the intro, each lyric line, the instrumental breaks), and how fast its
// marker crosses a line that has no word times.

import { chordsInLine, gapAfterLine, lineWords, sungBeats } from "../../shared/timeline";
import type { SongDoc } from "../../shared/types";

export interface Row {
  start: number;
  end: number;
  /** the words; empty for the intro and instrumental breaks */
  text: string;
  section?: string;
  /** chord changes a chord sheet put in the words (beat, column): where the words are at those
   *  beats, for lines without word times. The chords themselves are shown by the chord strip. */
  marks: { beat: number; col: number }[];
  /** when each word is sung (beats), when known */
  words?: number[];
  wordsEnd?: number;
}

/** The song as one row: the intro, each lyric line, and the instrumental breaks between them. */
export function ribbonRows(doc: SongDoc, length: number): Row[] {
  const rows: Row[] = [];
  const first = doc.lines[0]?.beat ?? length;
  if (first > 0) rows.push({ start: 0, end: first, text: "", section: doc.lines.length ? "Intro" : undefined, marks: [] });
  doc.lines.forEach((line, i) => {
    const gap = gapAfterLine(doc, i);
    const until = i + 1 < doc.lines.length ? doc.lines[i + 1].beat : Math.max(length, line.beat + 1);
    const words = lineWords(line);
    rows.push({
      start: line.beat,
      end: gap ? gap.start : until,
      text: line.text,
      section: line.section,
      marks: chordsInLine(doc, i).flatMap((p) => (p.carried || p.chord.col === undefined ? [] : [{ beat: p.chord.beat, col: p.col }])),
      ...(words ? { words, wordsEnd: line.wordsEnd } : {}),
    });
    if (gap) rows.push({ start: gap.start, end: gap.end, text: "", marks: [] });
  });
  return rows;
}

/** Beats per letter at which this song is sung: the middle value over its lines with word times,
 *  or null when none has them. */
export function singingPace(rows: Row[]): number | null {
  const paces = rows
    .flatMap((r) => (r.words?.length && r.wordsEnd !== undefined && r.text.length >= 8 ? [(r.wordsEnd - r.words[0]) / r.text.length] : []))
    .sort((a, b) => a - b);
  return paces.length ? paces[paces.length >> 1] : null;
}

/** Where the marker reaches the end of a line without word times: through its words at the
 *  song's pace (as long as a chord sheet's chords allow), then it waits for the next line. */
export function untimedEnd(row: Row, lastMark: number, pace: number | null, bpm: number): number {
  const settle = row.end - Math.min(0.5, (row.end - lastMark) * 0.15);
  if (row.marks.length) return settle;
  const sung = pace ? Math.max(1, row.text.length * pace) : sungBeats(row.text, bpm);
  return Math.min(settle, row.start + sung);
}
