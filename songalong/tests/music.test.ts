import { describe, expect, it } from "vitest";
import {
  chordNoteNames, displayChord, parseChord, simplifySymbol, spellingForKey, transposeSymbol,
} from "../shared/music/chords";
import { assignFingers, fretVoicings, pianoVoicing } from "../shared/music/voicings";
import { parseKey, keyPrefersFlats, transposeKey } from "../shared/music/notes";

const shape = (sym: string, inst: "guitar" | "ukulele" | "mandolin" = "guitar") =>
  fretVoicings(sym, inst, 1)[0]?.frets.map((f) => (f === null ? "x" : f.toString(36))).join("");

describe("chord parsing", () => {
  it("classifies chord types", () => {
    expect(parseChord("E5")?.category).toBe("power");
    expect(parseChord("Bbm")?.category).toBe("triad");
    expect(parseChord("Bbm7")?.category).toBe("seventh");
    expect(parseChord("Cmaj9")?.category).toBe("extended");
    expect(parseChord("Asus4")?.category).toBe("sus");
    expect(parseChord("Cadd9")?.category).toBe("added");
    expect(parseChord("note:E")?.category).toBe("note");
    expect(parseChord("notes:E G A")?.category).toBe("notes");
    expect(parseChord("N.C.")?.category).toBe("none");
    expect(parseChord("D/F#")?.bass).toBe(6);
    expect(parseChord("C6/9")?.suffix).toBe("69");
    expect(parseChord("Hello")).toBeNull();
    expect(parseChord("Chorus")).toBeNull();
    expect(parseChord("F#m7b5")?.suffix).toBe("m7b5");
    expect(parseChord("Bø")?.suffix).toBe("m7b5");
    expect(parseChord("CΔ7")?.suffix).toBe("maj7");
    expect(parseChord("Am(maj7)")?.suffix).toBe("mmaj7");
  });
  it("spells chord tones from the root letter", () => {
    expect(chordNoteNames(parseChord("Gb")!)).toEqual(["Gb", "Bb", "Db"]);
    expect(chordNoteNames(parseChord("Bbm7")!)).toEqual(["Bb", "Db", "F", "Ab"]);
    expect(chordNoteNames(parseChord("F#")!)).toEqual(["F#", "A#", "C#"]);
  });
  it("splits for display", () => {
    expect(displayChord("Bbm7")).toMatchObject({ root: "B♭", quality: "m", ext: "7" });
    expect(displayChord("D/F#")).toMatchObject({ root: "D", bass: "/F♯" });
  });
});

describe("transpose", () => {
  it("keeps spelling at 0 and follows the target key", () => {
    expect(transposeSymbol("Gb", 0, "sharps")).toBe("Gb");
    expect(transposeSymbol("Gb", 2, spellingForKey("Gb", 2))).toBe("Ab");
    expect(transposeSymbol("G", 1, spellingForKey("G", 1))).toBe("Ab");
    expect(transposeSymbol("D/F#", 2, spellingForKey("D", 2))).toBe("E/G#");
    expect(transposeSymbol("Bbm7", -2, "common")).toBe("G#m7");
    expect(transposeSymbol("Bb", -2, "common")).toBe("Ab");
    expect(transposeSymbol("notes:E G A", 2, "sharps")).toBe("notes:F# A B");
  });
  it("keys", () => {
    expect(keyPrefersFlats(parseKey("Gb"))).toBe(true);
    expect(keyPrefersFlats(parseKey("E"))).toBe(false);
    expect(keyPrefersFlats(parseKey("Em"))).toBe(false);
    expect(keyPrefersFlats(parseKey("Dm"))).toBe(true);
    expect(transposeKey(parseKey("G")!, 1).name).toBe("Ab");
    expect(transposeKey(parseKey("Am")!, 4).name).toBe("C#m");
  });
  it("simplifies", () => {
    expect(simplifySymbol("Bbm7")).toBe("Bbm");
    expect(simplifySymbol("Cmaj9")).toBe("C");
    expect(simplifySymbol("D/F#")).toBe("D");
    expect(simplifySymbol("E5")).toBe("E5");
    expect(simplifySymbol("Asus4")).toBe("A");
  });
});

describe("voicings", () => {
  it("uses the standard open shapes", () => {
    expect(shape("C")).toBe("x32010");
    expect(shape("G")).toBe("320003");
    expect(shape("Am")).toBe("x02210");
    expect(shape("F")).toBe("133211");
  });
  it("finds standard barre shapes by search", () => {
    expect(shape("Gb")).toBe("244322");
    expect(shape("F#")).toBe("244322");
    expect(shape("Bbm")).toBe("x13321");
    expect(shape("Bb")).toBe("x13331");
    expect(shape("Abm")).toBe("466444");
    expect(shape("C#m")).toBe("x46654");
  });
  it("power chords sit on the low strings", () => {
    expect(shape("E5")).toBe("022xxx");
    expect(shape("A5")).toBe("x022xx");
    expect(shape("C5")).toMatch(/^x355xx$|^8aaxxx$/);
  });
  it("prints a few searched shapes for review", () => {
    for (const s of ["Db", "Ebm", "Cbm", "Bbm7", "Fmaj7", "C#m7", "Bm7b5", "E7#9", "G/B", "Cadd9", "Dsus4", "Abmaj7", "note:E", "notes:E G A"]) {
      const vs = fretVoicings(s, "guitar", 3);
      console.log(s.padEnd(8), vs.map((v) => v.frets.map((f) => (f === null ? "x" : f)).join(" ") + ` (${v.difficulty})`).join("  |  "));
    }
    for (const s of ["C", "G", "Am", "F", "D", "Bb", "Ebm", "Gb", "E7", "Cmaj7"]) {
      console.log("uke", s.padEnd(6), fretVoicings(s, "ukulele", 2).map((v) => v.frets.join("")).join(" | "));
    }
    for (const s of ["G", "C", "D", "Em", "Bbm", "F#m", "A7", "Cmaj7"]) {
      console.log("mando", s.padEnd(6), fretVoicings(s, "mandolin", 2).map((v) => v.frets.join(" ")).join(" | "));
    }
  });
  it("fingers", () => {
    expect(assignFingers([null, 3, 2, 0, 1, 0])?.fingers).toEqual([null, 3, 2, null, 1, null]);
    expect(assignFingers([1, 3, 3, 2, 1, 1])?.barres[0]).toMatchObject({ fret: 1, from: 0, to: 5, finger: 1 });
    expect(assignFingers([1, 3, 3, 2, 1, 1])?.fingers).toEqual([1, 3, 4, 2, 1, 1]);
  });
  it("piano", () => {
    expect(pianoVoicing("C")?.right).toEqual([60, 64, 67]);
    expect(pianoVoicing("G")?.right).toEqual([62, 67, 71]);
    expect(pianoVoicing("D/F#")?.left).toBe(54);
  });
});

describe("lead notes with octaves", () => {
  it("parses and transposes octaves", async () => {
    const { noteMidi } = await import("../shared/music/chords");
    const c = parseChord("note:E4")!;
    expect(c.category).toBe("note");
    expect(noteMidi(c)).toBe(64);
    expect(transposeSymbol("note:B3", 1, "sharps")).toBe("note:C4");
    expect(transposeSymbol("notes:E4 G4", -2, "sharps")).toBe("notes:D4 F4");
    expect(transposeSymbol("note:E4", 12, "sharps")).toBe("note:E5");
    expect(parseChord("E4")?.suffix).toBe("sus4"); // a bare E4 is still the chord
  });
});

describe("bass", () => {
  it("shows chords as low-to-high shapes with the bass note lowest and labelled tones", async () => {
    const { fretVoicings, voicingMidi } = await import("../shared/music/voicings");
    for (const sym of ["G", "C", "A", "Em", "G7", "Cmaj7", "Bm7b5", "D/F#", "E5"]) {
      const v = fretVoicings(sym, "bass", 3);
      expect(v.length, sym).toBeGreaterThan(0);
      for (const x of v) {
        const m = voicingMidi(x, "bass");
        // rising across the strings, no doubled note
        const inOrder = x.frets.flatMap((f, i) => (f === null ? [] : [[28, 33, 38, 43][i] + f]));
        expect(inOrder).toEqual([...m]);
        expect(new Set(m).size).toBe(m.length);
        expect(x.labels).toBeDefined();
      }
    }
    // the bass note is lowest: the slash note for D/F#, the root otherwise
    expect(fretVoicings("D/F#", "bass")[0].labels!.find((l) => l !== null)).toBe("3");
    expect(fretVoicings("G", "bass")[0].labels!.find((l) => l !== null)).toBe("R");
    // major vs minor shows in the third; sevenths and flat fifths are in the shape
    expect(fretVoicings("E", "bass")[0].labels).toContain("3");
    expect(fretVoicings("Em", "bass")[0].labels).toContain("b3");
    expect(fretVoicings("G7", "bass")[0].labels).toContain("b7");
    expect(fretVoicings("Bm7b5", "bass")[0].labels).toContain("b5");
  });
  it("plays a melody in the bass's own range", async () => {
    const { fretVoicings, voicingMidi } = await import("../shared/music/voicings");
    const riff = fretVoicings("notes:E4 G4 A4", "bass");
    expect(riff.length).toBeGreaterThan(0);
    const m = voicingMidi(riff[0], "bass");
    expect(m.map((x) => x % 12)).toEqual([4, 7, 9]);
    expect(Math.max(...m)).toBeLessThan(58);
  });
});
