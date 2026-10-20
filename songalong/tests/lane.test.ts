import { describe, expect, it } from "vitest";
import { emptySong, type SongDoc } from "../shared/types";
import { ribbonRows, singingPace, untimedEnd } from "../src/song/ribbon";

// Made-up lyrics. A shout every two bars that nobody heard well enough to time, then a line with
// word times (sung at 0.2 beats a letter).
function song(): SongDoc {
  return {
    ...emptySong(),
    bpm: 120,
    beatsPerBar: 4,
    length: 64,
    chords: [
      { beat: 0, name: "B5" },
      { beat: 8, name: "E5" },
      { beat: 16, name: "B5" },
    ],
    lines: [
      { beat: 0, text: "Lightning" },
      { beat: 8, text: "Lightning" },
      { beat: 16, text: "Rolling over the hills tonight", words: [16, 17, 18, 19.5, 20.5], wordsEnd: 22 },
    ],
  };
}

describe("lyrics without word times", () => {
  it("are crossed at the song's own pace, then the marker waits for the next line", () => {
    const rows = ribbonRows(song(), 64);
    const pace = singingPace(rows);
    expect(pace).toBeCloseTo(6 / 30);
    const shout = rows[0];
    expect(shout.marks).toEqual([]); // the chord changes say nothing about where the words are
    expect(untimedEnd(shout, shout.start, pace, 120)).toBeCloseTo(1.8); // "Lightning", not two bars
  });

  it("go at an ordinary pace when no line has word times", () => {
    const doc = song();
    doc.lines = doc.lines.slice(0, 2);
    const rows = ribbonRows(doc, 64);
    expect(singingPace(rows)).toBeNull();
    expect(untimedEnd(rows[0], 0, null, 120)).toBeCloseTo(2.7); // 1.35 s at 120 BPM
  });

  it("follow a chord sheet's chords through the words", () => {
    const doc = song();
    doc.chords = [
      { beat: 0, name: "G", col: 0 },
      { beat: 4, name: "C", col: 5 },
    ];
    const rows = ribbonRows(doc, 64);
    expect(rows[0].marks).toEqual([
      { beat: 0, col: 0 },
      { beat: 4, col: 5 },
    ]);
    expect(untimedEnd(rows[0], 4, singingPace(rows), 120)).toBeCloseTo(7.5); // just before the next line
  });
});
