import { describe, expect, it } from "vitest";
import { fillPowerChords, powerChordOf } from "../shared/music/chordStyle";
import { parseKey } from "../shared/music/notes";
import { validatePracticeSettings } from "../shared/validate";
import type { ChordEvent } from "../shared/types";

/** Chords one after another, `beats` each. */
const progression = (names: string[], beats = 4): ChordEvent[] => names.map((name, i) => ({ beat: i * beats, name }));

describe("chord styles", () => {
  it("plays every chord as its root and fifth, as power chords", () => {
    expect(["C", "Am7", "Csus4", "G7", "D/F#", "E5"].map(powerChordOf)).toEqual(["C5", "A5", "C5", "G5", "D5", "E5"]);
    // no perfect fifth to play: as they are
    expect(["Bdim", "Caug", "Bm7b5"].map(powerChordOf)).toEqual(["Bdim", "Caug", "Bm7b5"]);
    expect(["note:E4", "notes:E4 G4", "N.C."].map(powerChordOf)).toEqual(["note:E4", "notes:E4 G4", "N.C."]);
  });

  it("fills in power chords from the key: its chords, a minor key's major V, and rock's borrowed chords major", () => {
    const inG = fillPowerChords(progression(["G5", "E5", "C5", "D5", "B5", "F5"]), undefined, parseKey("G"), 24);
    expect(Object.fromEntries(inG.chords)).toEqual({ G5: "G", E5: "Em", C5: "C", D5: "D", B5: "Bm", F5: "F" });
    expect(new Set(inG.by.values())).toEqual(new Set(["key"]));
    const inAm = fillPowerChords(progression(["A5", "G5", "F5", "E5", "D5", "C5"]), undefined, parseKey("Am"), 24);
    expect(Object.fromEntries(inAm.chords)).toEqual({ A5: "Am", G5: "G", F5: "F", E5: "E", D5: "Dm", C5: "C" });
    // other chords are left alone
    expect(fillPowerChords(progression(["G", "Em", "C5"]), undefined, parseKey("G"), 12).chords.has("Em")).toBe(false);
  });

  it("goes by the melody over a power chord when it clearly sings or plays a third", () => {
    const chords = progression(["G5", "E5", "C5", "D5"]);
    // over E5 (beats 4-8) the melody sings G# for two beats: E major, though the key says Em
    const lead: ChordEvent[] = [
      { beat: 4, name: "note:G#4" },
      { beat: 6, name: "note:B4" },
      // over C5 (beats 8-12): a riff with Eb in it, half a beat's worth: too little to go by
      { beat: 8, name: "notes:C4 Eb4" },
      { beat: 9, name: "N.C." },
    ];
    const filled = fillPowerChords(chords, lead, parseKey("G"), 16);
    expect(filled.chords.get("E5")).toBe("E");
    expect(filled.by.get("E5")).toBe("melody");
    expect(filled.chords.get("C5")).toBe("C");
    expect(filled.by.get("C5")).toBe("key");
    // both thirds about as much: the key decides
    const torn = fillPowerChords(chords, [{ beat: 4, name: "note:G4" }, { beat: 6, name: "note:G#4" }, { beat: 8, name: "N.C." }], parseKey("G"), 16);
    expect([torn.chords.get("E5"), torn.by.get("E5")]).toEqual(["Em", "key"]);
    // every time a power chord comes counts together
    const twice = fillPowerChords(progression(["A5", "D5", "A5", "D5"]), [{ beat: 8, name: "note:C5" }, { beat: 10, name: "N.C." }], parseKey("A"), 16);
    expect([twice.chords.get("A5"), twice.by.get("A5")]).toEqual(["Am", "melody"]);
  });

  it("keeps the chord style picked for a song", () => {
    expect(validatePracticeSettings({ chordStyle: "power" })).toEqual({ chordStyle: "power" });
    expect(validatePracticeSettings({ chordStyle: "full" })).toEqual({ chordStyle: "full" });
    // as recorded is the default, and anything else is let go
    expect(validatePracticeSettings({ chordStyle: "recorded" })).toEqual({});
    expect(validatePracticeSettings({ chordStyle: "jazz" })).toEqual({});
  });
});
