import { describe, expect, it } from "vitest";
import { detectChords } from "../src/analysis/detect";
import { synthSong, type SongSpec } from "./helpers/synth";

/** Share of beats whose detected chord matches the truth (root + major/minor family). */
function chordAccuracy(spec: SongSpec) {
  const song = synthSong(spec);
  const r = detectChords(song.samples, song.sampleRate);
  // map each true beat time to the detected chord sounding then
  let hit = 0;
  let n = 0;
  const family = (s: string) => s.replace(/(maj7|m7|7)$/, (m) => (m === "m7" ? "m" : ""));
  song.beatTimes.forEach((t, i) => {
    const truth = song.chordAtBeat[i];
    if (!truth) return;
    let k = 0;
    while (k + 1 < r.beatTimes.length && r.beatTimes[k + 1] <= t + 0.05) k++;
    let c = 0;
    while (c + 1 < r.chords.length && r.chords[c + 1].beat <= k) c++;
    n++;
    if (family(r.chords[c].name) === family(truth)) hit++;
  });
  return { r, song, accuracy: hit / Math.max(1, n) };
}

describe("chord detection on synthesized songs", () => {
  it("G D Em C at 120 BPM", () => {
    const { r, accuracy } = chordAccuracy({ bpm: 120, chords: ["G", "D", "Em", "C"], repeats: 4, ending: ["G"] });
    console.log("pop:", r.bpm, r.key, accuracy.toFixed(2), r.chords.slice(0, 8).map((c) => c.name).join(" "));
    expect(Math.abs(r.bpm - 120)).toBeLessThan(2.5);
    expect(r.key).toBe("G");
    expect(accuracy).toBeGreaterThan(0.85);
  });
  it("Am F C G at 90 BPM starting late", () => {
    const { r, accuracy } = chordAccuracy({ bpm: 90, chords: ["Am", "F", "C", "G"], repeats: 3, start: 0.37, instrument: "piano" });
    console.log("minor:", r.bpm, r.key, accuracy.toFixed(2), r.chords.slice(0, 8).map((c) => c.name).join(" "));
    expect(Math.abs(r.bpm - 90)).toBeLessThan(2);
    expect(["Am", "C"]).toContain(r.key);
    expect(accuracy).toBeGreaterThan(0.8);
  });
  it("finds the beat under an accented 3+3+2 strum, not the accents", () => {
    // 16th-note strumming accented on 1, the "a" of 1 and the "&" of 2: the accents repeat every
    // three 16ths, which once read as 116 or 130 BPM for an 87 BPM song
    const strum = [1, 0.2, 0.2, 0.9, 0.2, 0.2, 0.9, 0.35];
    for (const bpm of [75, 87, 95]) {
      const { r, accuracy } = chordAccuracy({ bpm, chords: ["F#m7", "A", "E", "B7"], repeats: 5, instrument: "guitar", strum, kick: "none", bass: "none" });
      console.log("strum:", bpm, "->", r.bpm, accuracy.toFixed(2));
      expect(Math.abs(r.bpm - bpm)).toBeLessThan(2);
    }
  });
  it("uses the chord changes when accents run the whole bar", () => {
    // accents every three 16ths across the bar (3+3+3+3+2+2): the rhythm alone reads 4/3 too fast,
    // but the chords change once a bar
    const bar = (accents: number[], ghost: number) => Array.from({ length: 16 }, (_, i) => (accents.includes(i) ? 1 : ghost));
    for (const strum of [bar([0, 3, 6, 9, 12, 14], 0.12), bar([0, 3, 6, 10, 13], 0.15)]) {
      const { r } = chordAccuracy({ bpm: 87, chords: ["F#m7", "A", "E", "B7"], repeats: 6, instrument: "guitar", strum, kick: "none", bass: "none" });
      console.log("bar accents:", r.bpm);
      expect(Math.abs(r.bpm - 87)).toBeLessThan(2);
    }
  });
  it("spells flats in a flat key", () => {
    const { r } = chordAccuracy({ bpm: 100, chords: ["Eb", "Bb", "Cm", "Ab"], repeats: 3, ending: ["Eb"], instrument: "guitar" });
    console.log("flat:", r.bpm, r.key, r.chords.slice(0, 8).map((c) => c.name).join(" "));
    expect(r.chords.some((c) => c.name.startsWith("Bb"))).toBe(true);
    expect(r.chords.some((c) => c.name.includes("#"))).toBe(false);
  });
  it("names power chords on a distorted rock guitar, and keeps full chords full", () => {
    const rock = { bpm: 132, repeats: 3, instrument: "guitar" as const, distortion: 8, snare: true, hats: true, bass: "eighths" as const };
    const power = chordAccuracy({ ...rock, chords: ["E5", "D5", "A5", "E5:2", "G5:2"] });
    const names = power.r.chords.map((c) => c.name).filter((n) => n !== "N.C.");
    console.log("power:", power.r.bpm, names.slice(0, 10).join(" "));
    // the right roots, all as power chords
    expect(power.accuracy).toBeGreaterThanOrEqual(0.85);
    expect(names.filter((n) => n.endsWith("5")).length).toBeGreaterThanOrEqual(0.9 * names.length);
    const full = chordAccuracy({ ...rock, chords: ["E", "D", "A", "Em:2", "G:2"] });
    const fullNames = full.r.chords.map((c) => c.name).filter((n) => n !== "N.C.");
    console.log("full:", fullNames.slice(0, 10).join(" "));
    expect(full.accuracy).toBeGreaterThanOrEqual(0.85);
    expect(fullNames.some((n) => n.endsWith("5"))).toBe(false);
    // one power chord in a song of full chords: named as a full chord, like the rest
    const mixed = chordAccuracy({ ...rock, chords: ["E5", "D", "A", "G"] });
    expect(mixed.r.chords.some((c) => c.name.endsWith("5"))).toBe(false);
  });
});
