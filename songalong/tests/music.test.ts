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
  it("shows one note to play per chord, with its fifth and octave as options", async () => {
    const { fretVoicings, voicingMidi } = await import("../shared/music/voicings");
    const BASS = [28, 33, 38, 43];
    const at = (v: { frets: (number | null)[] }, s: number) => BASS[s] + v.frets[s]!;
    for (const sym of ["G", "C", "A", "Em", "G7", "Cmaj7", "Bm7b5", "D/F#", "E5", "Bb", "F#m"]) {
      const v = fretVoicings(sym, "bass", 3);
      expect(v.length, sym).toBeGreaterThan(0);
      for (const x of v) {
        const played = voicingMidi(x, "bass");
        expect(played, sym).toHaveLength(1); // only the note to play sounds
        const s = x.frets.findIndex((f, i) => f !== null && !x.extra![i]);
        expect(played[0]).toBe(at(x, s));
        // the options: the fifth a string up, the octave two strings up, both two frets higher
        if (x.frets[s + 1] != null) expect([at(x, s + 1) - played[0], x.extra![s + 1]]).toEqual([7, true]);
        if (x.frets[s + 2] != null) expect([at(x, s + 2) - played[0], x.extra![s + 2]]).toEqual([12, true]);
      }
    }
    const first = (sym: string) => fretVoicings(sym, "bass")[0];
    // where bass players play them: G on the E string's 3rd fret, open E and A, C on the A string
    expect(first("G").frets).toEqual([3, 5, 5, null]);
    expect(first("G").labels).toEqual(["G", "D", "G", null]);
    expect(first("E").frets).toEqual([0, 2, 2, null]);
    expect(first("A").frets).toEqual([null, 0, 2, 2]);
    expect(first("C").frets).toEqual([null, 3, 5, 5]);
    expect(first("D").frets).toEqual([null, 5, 7, 7]); // room for the octave above
    // a slash chord plays its bass note, named the way the chord spells it
    expect(voicingMidi(first("D/F#"), "bass")).toEqual([30]);
    expect(first("D/F#").labels![0]).toBe("F♯");
    expect(first("Bb").labels).toContain("B♭");
    // minor, seventh and power chords: still the root
    expect(voicingMidi(first("Em7"), "bass")).toEqual([28]);
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

describe("tuner", () => {
  /** A plucked-string-like tone: a few harmonics, a little noise. */
  function tone(freq: number, sr: number, seconds = 0.2) {
    const n = Math.round(sr * seconds);
    const out = new Float32Array(n);
    for (let i = 0; i < n; i++) {
      const t = i / sr;
      out[i] = 0.5 * Math.sin(2 * Math.PI * freq * t) + 0.3 * Math.sin(2 * Math.PI * 2 * freq * t + 0.4) + 0.15 * Math.sin(2 * Math.PI * 3 * freq * t + 1.1) + 0.01 * (Math.random() * 2 - 1);
    }
    return out;
  }
  const cents = (f: number, ref: number) => 1200 * Math.log2(f / ref);

  it("reads guitar strings to within a cent", async () => {
    const { detectPitch } = await import("../src/tools/pitch");
    const sr = 48000;
    for (const [f0, off] of [[82.41, 0], [110, 7], [146.83, -12], [196, 3], [246.94, -4], [329.63, 20], [440, 0]]) {
      const f = f0 * Math.pow(2, off / 1200);
      const got = detectPitch(tone(f, sr).subarray(0, 4096), sr)!;
      expect(Math.abs(cents(got, f)), `${f0} ${off}`).toBeLessThan(1);
    }
  });

  it("reads bass strings (down to the low E) after the 4x averaging", async () => {
    const { detectPitch } = await import("../src/tools/pitch");
    const sr = 48000;
    for (const [f0, off] of [[41.2, 0], [41.2, -9], [55, 5], [73.42, 0], [98, -3]]) {
      const f = f0 * Math.pow(2, off / 1200);
      const buf = tone(f, sr).subarray(0, 8192);
      const small = new Float32Array(2048);
      for (let i = 0; i < small.length; i++) small[i] = 0.25 * (buf[4 * i] + buf[4 * i + 1] + buf[4 * i + 2] + buf[4 * i + 3]);
      const got = detectPitch(small, sr / 4, 35, 400)!;
      expect(Math.abs(cents(got, f)), `${f0} ${off}`).toBeLessThan(1.5);
    }
  });
});
