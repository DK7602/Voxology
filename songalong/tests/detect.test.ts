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
  const family = (s: string) => s.replace(/(maj7|m7|7)$/, (m) => (m === "m7" ? "m" : "")).replace(/^([A-G][#b]?)$/, "$1");
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
  it("spells flats in a flat key", () => {
    const { r } = chordAccuracy({ bpm: 100, chords: ["Eb", "Bb", "Cm", "Ab"], repeats: 3, ending: ["Eb"], instrument: "guitar" });
    console.log("flat:", r.bpm, r.key, r.chords.slice(0, 8).map((c) => c.name).join(" "));
    expect(r.chords.some((c) => c.name.startsWith("Bb"))).toBe(true);
    expect(r.chords.some((c) => c.name.includes("#"))).toBe(false);
  });
});
