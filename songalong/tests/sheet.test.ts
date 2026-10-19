import { describe, expect, it } from "vitest";
import { DEMO_SONGS } from "../shared/demoSongs";
import { exportChordPro, importChordSheet } from "../shared/chordpro";
import { parseChord } from "../shared/music/chords";
import { bestCapo, capoOptions } from "../shared/music/capo";
import { songToMidi } from "../shared/midi";
import { Timeline, bars, chordsInLine, indexAt, lineWords } from "../shared/timeline";
import { validateSongDoc } from "../shared/validate";
import type { SongDoc } from "../shared/types";

describe("demo songs", () => {
  it("parse into valid documents", () => {
    for (const d of DEMO_SONGS) {
      expect(d.id.startsWith("demo-")).toBe(true);
      expect(d.doc.chords.length).toBeGreaterThan(10);
      for (const c of d.doc.chords) expect(parseChord(c.name), c.name).not.toBeNull();
      for (let i = 1; i < d.doc.chords.length; i++) expect(d.doc.chords[i].beat).toBeGreaterThan(d.doc.chords[i - 1].beat);
      expect(d.doc.length).toBeGreaterThan(d.doc.chords.at(-1)!.beat);
    }
    const iron = DEMO_SONGS.find((d) => d.id === "demo-iron-sparks")!.doc;
    expect(iron.chords[0].name).toBe("notes:E G A");
  });
  it("places chords over the right words", () => {
    const porch = DEMO_SONGS[0].doc;
    const verse = porch.lines.findIndex((l) => l.text.startsWith("Left the porch"));
    const placed = chordsInLine(porch, verse);
    expect(placed.map((p) => [p.chord.name, porch.lines[verse].text.slice(p.col, p.col + 3)])).toEqual([
      ["Gb", "Lef"],
      ["Db", "by "],
    ]);
  });
});

describe("timeline", () => {
  it("maps beats and seconds both ways", () => {
    const doc = { ...DEMO_SONGS[0].doc, bpm: 120, offset: 1 };
    const t = new Timeline(doc);
    expect(t.beatToTime(4)).toBeCloseTo(3);
    expect(t.timeToBeat(3)).toBeCloseTo(4);
    const measured = { ...doc, beatTimes: [0.5, 1, 1.6, 2.1] };
    const m = new Timeline(measured);
    expect(m.timeToBeat(1.3)).toBeCloseTo(1.5);
    expect(m.beatToTime(2.5)).toBeCloseTo(1.85);
    expect(m.beatToTime(4)).toBeCloseTo(2.6);
  });
  it("finds the sounding chord and bars", () => {
    const doc = DEMO_SONGS[0].doc;
    expect(doc.chords[indexAt(doc.chords, 5)].name).toBe("Db");
    const b = bars(doc, doc.length);
    expect(b.length).toBe(doc.length / 4);
    expect(b[0].changes[0].chordIndex).toBe(0);
  });
  it("puts chords over the word being sung when the line has word times", () => {
    const doc: SongDoc = {
      ...DEMO_SONGS[0].doc,
      bpm: 120,
      chords: [
        { beat: 0, name: "G" },
        { beat: 5.2, name: "D" }, // early in "boats"
        { beat: 7.4, name: "Em" }, // late in the gap after "along": goes over "the"
        { beat: 12, name: "C" }, // after the last word ends
      ],
      // "Paper boats along the river": a long "river" held to beat 11
      lines: [{ beat: 4, text: "Paper boats along the river", words: [4, 5, 6, 8, 8.5], wordsEnd: 11 }],
      length: 16,
    };
    const cols = chordsInLine(doc, 0).map((p) => [p.chord.name, p.col, p.carried]);
    expect(cols).toEqual([
      ["G", 0, true],
      ["D", 6, false],
      ["Em", 18, false],
      ["C", 27, false],
    ]);
    // a line moved since its words were timed leaves the word times out
    expect(lineWords({ ...doc.lines[0], beat: 6 })).toBeNull();
    expect(lineWords({ ...doc.lines[0], text: "Paper boats" })).toBeNull();
    // saving keeps word times only when there is one per word, in order
    const saved = validateSongDoc({ ...doc, lines: [doc.lines[0], { beat: 20, text: "two words", words: [21] }, { beat: 30, text: "out of order", words: [30, 32, 31] }] });
    expect(saved.lines[0]).toMatchObject({ words: [4, 5, 6, 8, 8.5], wordsEnd: 11 });
    expect(saved.lines[1].words).toBeUndefined();
    expect(saved.lines[2].words).toBeUndefined();
  });
});

describe("chord sheets", () => {
  it("imports chords over lyrics", () => {
    const { doc } = importChordSheet(`{title: Test}
{key: G}
[Verse 1]
G           D
Hello there my friend
Em    C
How are you
`);
    expect(doc.title).toBe("Test");
    expect(doc.lines[0]).toMatchObject({ text: "Hello there my friend", section: "Verse 1" });
    expect(doc.chords.map((c) => c.name)).toEqual(["G", "D", "Em", "C"]);
    expect(doc.chords[1].col).toBe(12);
    expect(doc.chords[2].beat).toBe(8);
  });
  it("imports ChordPro inline chords and round-trips", () => {
    const src = `{title: Round}
{comment: Chorus}
[C]Over the [G]hill and [Am]far a[F]way
`;
    const { doc } = importChordSheet(src);
    expect(doc.lines[0].section).toBe("Chorus");
    expect(doc.chords.map((c) => c.col)).toEqual([0, 9, 18, 23]);
    const out = exportChordPro(doc);
    expect(out).toContain("[C]Over the [G]hill and [Am]far a[F]way");
  });
});

describe("capo + midi", () => {
  it("recommends a capo that turns Gb into easy shapes", () => {
    const doc = DEMO_SONGS[0].doc;
    const best = bestCapo(doc.chords, doc.length);
    expect(best.capo).toBeGreaterThan(0);
    const opts = capoOptions(doc.chords, doc.length);
    expect(opts[best.capo].difficulty).toBeLessThan(opts[0].difficulty);
    console.log(opts.map((o) => `capo ${o.capo}: ${o.difficulty} ${o.shapes.join(" ")}`).join("\n"));
  });
  it("writes a valid MIDI file", () => {
    const midi = songToMidi(DEMO_SONGS[2].doc, 2);
    expect(String.fromCharCode(...midi.slice(0, 4))).toBe("MThd");
    expect(midi[11]).toBe(3); // three tracks
    expect(midi.length).toBeGreaterThan(200);
  });
});

describe("lead part", () => {
  it("imports and exports the lead text format", async () => {
    const { importLead, exportLead, leadPositions } = await import("../shared/lead");
    const { events, warnings } = importLead("bar 2: E4 G4:0.5 A4:0.5 r [E5]:2 | B3-A3-G3:2", 4);
    expect(warnings).toEqual([]);
    expect(events.map((e) => [e.beat, e.name])).toEqual([
      [4, "note:E4"], [5, "note:G4"], [5.5, "note:A4"], [6, "N.C."], [7, "E5"], [9, "notes:B3 A3 G3"], [11, "N.C."],
    ]);
    const text = exportLead(events, 4);
    expect(importLead(text, 4).events).toEqual(events);
    const spots = leadPositions(events.map((e) => e.name), events.map((e) => e.beat), "guitar", 4);
    // E4 on guitar: open high E or 5th fret B string etc.; must sound E4 (MIDI 64)
    expect(spots[0][0].midi).toBe(64);
    expect(spots[0][0].fret).toBeLessThanOrEqual(9);
  });
  it("demo songs carry lead parts", () => {
    const iron = DEMO_SONGS.find((d) => d.id === "demo-iron-sparks")!.doc;
    expect(iron.lead!.length).toBeGreaterThan(20);
    for (const e of iron.lead!) expect(parseChord(e.name), e.name).not.toBeNull();
  });
});
