import { describe, expect, it } from "vitest";
import { resample } from "../src/analysis/dsp";
import { midiName } from "../shared/music/notes";
import { leadPart, topLine } from "../prep/melody";
import { loadNoteModel } from "../prep/noteModel";
import { notesFrom, NOTES_RATE, transcribe, type Note } from "../prep/notes";
import { synthSong } from "./helpers/synth";

const note = (start: number, end: number, pitch: number, amp = 0.6): Note => ({ start, end, pitch, amp });

describe("notes from the model's output", () => {
  it("turns onsets and the frames after them into notes", () => {
    const n = 120;
    const frames = new Float32Array(n * 88);
    const onsets = new Float32Array(n * 88);
    // key 48 (A4): starts at frame 10, sounds to 60; key 51: sounds 70-100 with no onset given
    for (let t = 10; t < 60; t++) frames[t * 88 + 48] = 0.8;
    onsets[10 * 88 + 48] = 0.9;
    for (let t = 70; t < 100; t++) frames[t * 88 + 51] = 0.7;
    const out = notesFrom(frames, onsets, n);
    expect(out.map(([a, b, k]) => [a, b, k])).toEqual([
      [10, 60, 48],
      [70, 100, 51],
    ]);
    // too short to be a note
    const blip = new Float32Array(n * 88);
    for (let t = 20; t < 28; t++) blip[t * 88 + 40] = 0.9;
    expect(notesFrom(blip, new Float32Array(n * 88), n)).toEqual([]);
  });
});

describe("the lead part", () => {
  it("keeps the top line and leaves strummed chords out", () => {
    const notes = [
      // a sung note over a strum, with an overtone far above the lead's range
      note(5, 5.5, 74), note(5.02, 5.5, 93, 0.5), note(5.01, 5.5, 55), note(5.02, 5.5, 59), note(5.03, 5.5, 62),
      // a strummed chord: four notes within 40 ms
      note(0, 1, 52), note(0.01, 1, 56), note(0.02, 1, 59), note(0.03, 1, 64),
      // a melody with a lower note under each of its first two notes
      note(1, 1.5, 72), note(1.01, 1.5, 60), note(1.5, 2, 74), note(1.52, 2, 62), note(2, 2.4, 76),
      // too low, too faint
      note(3, 3.5, 40), note(3.5, 4, 70, 0.2),
    ];
    expect(topLine(notes).map((n) => n.pitch)).toEqual([72, 74, 76, 74]);
  });

  it("writes it on the beats: notes, quick runs and rests", () => {
    const slow = Array.from({ length: 16 }, (_, i) => i); // 60 BPM: a 16th is a quarter second
    const notes = [note(1, 1.5, 72), note(1.5, 2, 74), note(2, 2.05, 76), note(2.05, 2.1, 77), note(2.1, 2.4, 79), note(4, 5, 69)];
    expect(leadPart(notes, slow, false)).toEqual([
      { beat: 1, name: "note:C5" },
      { beat: 1.5, name: "note:D5" },
      // three notes inside one 16th: a quick run
      { beat: 2, name: "notes:E5 F5 G5" },
      { beat: 2.5, name: "N.C." },
      { beat: 4, name: "note:A4" },
      { beat: 5, name: "N.C." },
    ]);
    // a note a little late on an 8th goes to the 8th; a real 16th stays; a repeat is one note
    const late = [note(1.14, 1.3, 72), note(1.3, 1.48, 72), note(1.75, 2, 74)];
    expect(leadPart(late, slow, false).map((e) => [e.beat, e.name])).toEqual([
      [1, "note:C5"],
      [1.5, "N.C."],
      [1.75, "note:D5"],
      [2, "N.C."],
    ]);
  });
});

describe("a lead part from audio", () => {
  /** Share of the true lead notes the found lead part plays on their 16th, and share of the lead
   *  part's notes that are true ones. */
  async function score(spec: Parameters<typeof synthSong>[0]) {
    const model = await loadNoteModel();
    expect(model).not.toBeNull();
    const song = synthSong(spec);
    const notes = await transcribe(resample(song.samples, song.sampleRate, NOTES_RATE), model!);
    const lead = leadPart(notes, song.beatTimes, false);
    const spb = 60 / song.bpm;
    const slotOf = (t: number) => Math.round(t / spb / 0.25) * 0.25;
    const found = song.melodyNotes.filter((m) => lead.some((e) => Math.abs(e.beat - slotOf(m.time)) <= 0.25 && e.name.includes(midiName(m.midi)))).length;
    const played = lead.filter((e) => e.name !== "N.C.");
    const right = played.filter((e) => song.melodyNotes.some((m) => Math.abs(e.beat - slotOf(m.time)) <= 0.25 && e.name.includes(midiName(m.midi)))).length;
    return { recall: found / song.melodyNotes.length, precision: right / Math.max(1, played.length) };
  }

  it("finds a picked guitar riff over pads, bass and drums", async () => {
    // an arpeggio riff in 8ths: root, octave, fifth, fourth, octave + fourth, fifth, major 7th, fifth
    const s = await score({ bpm: 120, chords: ["D", "C", "G", "D"], repeats: 2, riff: [0, 12, 7, 5, 17, 7, 16, 7], snare: true, hats: true, seed: 5 });
    console.log("riff: found", s.recall.toFixed(2), "right", s.precision.toFixed(2));
    // the misses: riff notes landing with a chord change, among the chord's own notes
    expect(s.recall).toBeGreaterThanOrEqual(0.75);
    expect(s.precision).toBeGreaterThanOrEqual(0.8);
  }, 60_000);

  it("follows a sung melody over strummed guitar, roughly", async () => {
    const s = await score({ bpm: 100, chords: ["C", "Am", "F", "G"], repeats: 2, instrument: "guitar", melody: true, seed: 3 });
    console.log("sung: found", s.recall.toFixed(2), "right", s.precision.toFixed(2));
    expect(s.recall).toBeGreaterThanOrEqual(0.4);
  }, 60_000);
});
