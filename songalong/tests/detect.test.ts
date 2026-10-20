import { describe, expect, it } from "vitest";
import { mendSlips } from "../src/analysis/beats";
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
  it("stays on the beat when the up-strums get louder for a while", () => {
    // an acoustic cover strummed down-up in 8ths at 74 BPM: four bars in the middle accent the
    // up-strums (the "and"s). The beat must not slide half a beat onto them and back.
    const downs = [1, 0, 0.45, 0, 0.9, 0, 0.45, 0];
    const ups = [0.4, 0, 1, 0, 0.35, 0, 1, 0];
    const part = (strum: number[], repeats: number) =>
      synthSong({ bpm: 74, chords: ["G", "D", "Em", "C"], repeats, instrument: "guitar", strum, kick: "none", bass: "none", tail: 0 });
    const parts = [part(downs, 2), part(ups, 1), part(downs, 2)];
    const samples = new Float32Array(parts.reduce((n, p) => n + p.samples.length, 0));
    let at = 0;
    for (const p of parts) {
      samples.set(p.samples, at);
      at += p.samples.length;
    }
    const r = detectChords(samples, parts[0].sampleRate);
    const spb = 60 / 74;
    // every beat found sits on a played beat (half a beat would be 405 ms off)
    const off = r.beatTimes.filter((t) => t > 0.5 && t < at / parts[0].sampleRate - 0.5).map((t) => Math.abs(t / spb - Math.round(t / spb)) * spb);
    console.log("up-strums:", r.bpm, "worst", Math.round(1000 * Math.max(...off)), "ms");
    expect(Math.max(...off)).toBeLessThan(0.08);
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

describe("mending beats that slid off the beat", () => {
  const P = 0.8;
  // a steady pulse with a little human unevenness
  const steady = (n: number, from = 0) => Array.from({ length: n }, (_, i) => from + i * P + 0.012 * Math.sin(i * 2.3));
  /** beats that slide by `by` over three short (or long) beats after beat `at` */
  const slid = (beats: number[], at: number, by: number) => beats.map((t, i) => (i <= at ? t : t + by * Math.min(1, (i - at) / 3)));

  it("leaves a steady pulse alone", () => {
    const b = steady(60);
    expect(mendSlips(b)).toEqual(b);
  });

  it("moves a stretch that slid half a beat back, and leaves the rest", () => {
    const truth = steady(80);
    const b = slid(slid(truth, 30, -0.42), 50, 0.42); // off the beat for 20 beats, then back
    const m = mendSlips(b);
    const off = m.map((t, i) => Math.abs(t - truth[i]));
    expect(Math.max(...off.slice(0, 31))).toBe(0);
    expect(Math.max(...off.slice(34, 51))).toBeLessThan(0.03); // back on the beat (by half a beat)
    expect(Math.max(...off.slice(54))).toBe(0);
  });

  it("goes with most of the song when the slide lasts to the end", () => {
    const truth = steady(90);
    const m = mendSlips(slid(truth, 60, -0.42));
    expect(Math.abs(m[80] - truth[80])).toBeLessThan(0.03);
    // ...but when most of the song is after the slide, that part is the beat
    const late = slid(truth, 20, -0.42);
    expect(mendSlips(late)[80]).toBe(late[80]);
  });

  it("leaves a real change of tempo, and a slow ending, alone", () => {
    const faster = [...steady(40), ...steady(40, 40 * P).map((t, i) => 40 * P + (t - 40 * P) * 0.85 - (i ? 0 : 0))];
    expect(mendSlips(faster)).toEqual(faster);
    const b = steady(50);
    const ending = [...b, ...[1.1, 1.25, 1.45, 1.7].map((x, i, a) => b[49] + a.slice(0, i + 1).reduce((s, y) => s + y * P, 0))];
    expect(mendSlips(ending)).toEqual(ending);
  });
});
