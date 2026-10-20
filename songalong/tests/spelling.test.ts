import { describe, expect, it } from "vitest";
import { displayChord, enharmonicSpellings, transposeSymbol } from "../shared/music/chords";
import { shapeFor } from "../shared/music/capo";
import { parseKey, prettyAccidentals, spellInKey, usualKey } from "../shared/music/notes";
import { keyFromChords, listedChords, namedForList, songKey } from "../shared/music/songKey";
import { fretVoicings, pianoVoicing } from "../shared/music/voicings";

const key = (name: string) => {
  const k = parseKey(name)!;
  return usualKey(k.tonic, k.minor);
};
const inKey = (sym: string, k: string) => transposeSymbol(sym, 0, key(k));

describe("naming notes in a key", () => {
  it("names keys the usual way", () => {
    expect(usualKey(6, false).name).toBe("F#");
    expect(usualKey(1, false).name).toBe("Db");
    expect(usualKey(3, true).name).toBe("Ebm");
    expect(usualKey(8, true).name).toBe("G#m");
    expect(key("Gb").name).toBe("F#");
  });

  it("uses the key's letters, flats for borrowed notes and sharp 4ths", () => {
    const C = key("C");
    expect([10, 8, 3, 1, 6].map((pc) => spellInKey(pc, C))).toEqual(["Bb", "Ab", "Eb", "Db", "F#"]);
    expect(spellInKey(10, key("G"))).toBe("Bb");
    expect(spellInKey(6, key("G"))).toBe("F#");
    // minor keys raise the 3rd, 4th, 6th and 7th
    expect([8, 1, 3, 6, 0].map((pc) => spellInKey(pc, key("Am")))).toEqual(["G#", "C#", "D#", "F#", "C"]);
    expect(spellInKey(10, key("Am"))).toBe("Bb");
  });

  it("names a note that leads up a half step as a sharp", () => {
    expect(spellInKey(1, key("C"), true)).toBe("C#");
    expect(spellInKey(8, key("C"), true)).toBe("G#");
  });

  it("never writes Cb, Fb, E# or B#", () => {
    expect(spellInKey(5, key("F#"))).toBe("F");
    expect(spellInKey(11, key("Ebm"))).toBe("B");
    expect(spellInKey(0, key("C#m"))).toBe("C");
  });
});

describe("chords named for the key", () => {
  it("renames chords even without transposing", () => {
    expect(inKey("A#", "G")).toBe("Bb");
    expect(inKey("G#", "C")).toBe("Ab");
    expect(inKey("D/Gb", "C")).toBe("D/F#");
    expect(inKey("Gb", "F#")).toBe("F#");
    expect(inKey("Cb", "F#")).toBe("B");
    expect(inKey("Ebm", "F#")).toBe("D#m");
  });

  it("names diminished roots and slash basses by what they do", () => {
    expect(inKey("Abdim", "C")).toBe("G#dim");
    expect(inKey("Dbdim7", "C")).toBe("C#dim7");
    // a chord tone is named from the root, a half step under the root leads up to it
    expect(inKey("E/Ab", "C")).toBe("E/G#");
    expect(inKey("Am/Ab", "C")).toBe("Am/G#");
    expect(inKey("C/A#", "C")).toBe("C/Bb");
  });

  it("transposes into the new key's names", () => {
    expect(transposeSymbol("E/G#", -2, key("C"))).toBe("D/F#");
    expect(transposeSymbol("Bb", 2, key("E"))).toBe("C");
    expect(transposeSymbol("F", 1, key("E"))).toBe("F#");
    expect(transposeSymbol("Bb", 0, "sharps")).toBe("Bb");
  });

  it("names lead notes for the key, keeping octaves", () => {
    expect(inKey("note:A#4", "F")).toBe("note:Bb4");
    expect(inKey("notes:D#4 F#4 Gb4", "E")).toBe("notes:D#4 F#4 F#4");
  });

  it("names capo shapes for the shapes' key", () => {
    const E = key("E");
    expect(["F#", "C#", "D#m", "G#m", "B"].map((c) => shapeFor(c, 2, E))).toEqual(["E", "B", "C#m", "F#m", "A"]);
    expect(shapeFor("G", 1, key("D"))).toBe("F#");
  });
});

describe("the song's key", () => {
  const list = (s: string) => listedChords(s.split(" "));
  it("is found from the chords", () => {
    expect(keyFromChords(list("G C D Em G"))?.name).toBe("G");
    expect(keyFromChords(list("C Am F G C"))?.name).toBe("C");
    expect(keyFromChords(list("Am F C G Am"))?.name).toBe("Am");
    expect(keyFromChords([])).toBeNull();
  });

  it("is the given key unless the chords clearly say otherwise", () => {
    expect(songKey("E", list("E D A E"))?.name).toBe("E");
    expect(songKey("Gb", list("Gb Db Ebm Cb Gb"))?.name).toBe("F#");
    // a pasted sheet that kept the default key of C
    expect(songKey("C", list("E A B C#m G#m F#m E"))?.name).toBe("E");
    expect(songKey("", list("C F G C"))?.name).toBe("C");
    expect(songKey("", [])).toBeNull();
  });

  it("names song lists the same way", () => {
    expect(namedForList("G", ["G", "A#", "C", "D"])).toEqual({ key: "G", chords: ["G", "B♭", "C", "D"] });
  });
});

describe("labels and display", () => {
  it("shows ♯ and ♭ in extensions too", () => {
    expect(prettyAccidentals("m7b5")).toBe("m7♭5");
    expect(prettyAccidentals("7#9")).toBe("7♯9");
    expect(prettyAccidentals("1 b3 5")).toBe("1 ♭3 5");
    expect(displayChord("C7b9").ext).toBe("7♭9");
  });

  it("names piano keys and bass notes as the chord spells them", () => {
    const names = pianoVoicing("F#")!.names!;
    expect([names.get(6), names.get(10), names.get(1)]).toEqual(["F#", "A#", "C#"]);
    const bass = fretVoicings("Bb", "bass", 1)[0].labels!.filter(Boolean);
    expect(bass).toEqual(expect.arrayContaining(["B♭", "F"]));
    expect(bass).not.toContain("A♯");
  });

  it("finds a chord however a song spells it", () => {
    expect(enharmonicSpellings("Bb7")).toEqual(expect.arrayContaining(["Bb7", "A#7"]));
    expect(enharmonicSpellings("D/F#")).toEqual(expect.arrayContaining(["D/F#", "D/Gb"]));
    expect(enharmonicSpellings("love")).toEqual(["love"]);
  });
});
