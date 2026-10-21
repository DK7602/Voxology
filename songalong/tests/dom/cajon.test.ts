import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

const played: { stroke: string; when: number }[] = [];
const strummed: number[] = [];
vi.mock("../../src/player/synth", () => ({
  audioContext: () => ({ currentTime: 0 }),
  unlockAudio: async () => {},
  click() {},
  strum: () => void strummed.push(1),
  arpeggio() {},
  cajonHit: (stroke: string, when: number) => void played.push({ stroke, when }),
}));

import { cajonPart, cajonStrokes } from "../../shared/music/cajon";
import { DEMO_SONGS } from "../../shared/demoSongs";
import { PlayerEngine } from "../../src/player/engine";
import { FakeMedia } from "./fakeMedia";

beforeEach(() => {
  played.length = 0;
  strummed.length = 0;
  vi.stubGlobal("window", { setInterval: () => 0, clearInterval() {}, setTimeout: () => 0 });
  vi.stubGlobal("requestAnimationFrame", () => 0);
  vi.stubGlobal("cancelAnimationFrame", () => {});
});

afterEach(() => vi.unstubAllGlobals());

describe("playing the cajon part", () => {
  it("plays the cajon strokes instead of the chords, each once, on time", async () => {
    const doc = DEMO_SONGS[0].doc;
    const engine = new PlayerEngine(doc);
    const media = new FakeMedia();
    engine.setMedia(media);
    engine.setCountIn(0);
    engine.setBacking(true);
    engine.instrument = "cajon";
    engine.voicer = () => ({ midis: [40, 45, 50], riff: false });
    const strokes = cajonStrokes(cajonPart({ beatsPerBar: doc.beatsPerBar, bpm: doc.bpm, length: doc.length }, "easy"));
    engine.setCajon(strokes);
    engine.seekBeat(0);
    media.now = engine.timeline.beatToTime(0);
    await engine.play();
    // walk through two bars, a little at a time, as the scheduler does
    const end = engine.timeline.beatToTime(8);
    for (let t = media.now; t < end; t += 0.05) {
      media.now = t;
      (engine as unknown as { schedule(): void }).schedule();
    }
    expect(strummed).toHaveLength(0);
    // Easy, suggested: Bass on 1 and 3, Slap on 2 and 4 (the next bar's first Bass is already
    // scheduled: the scheduler looks a little ahead)
    expect(played.map((p) => p.stroke).join("")).toBe("BSBSBSBSB");
    // each sent to the audio clock ahead of its time (by at most the look-ahead), never late
    for (const p of played) {
      expect(p.when).toBeGreaterThanOrEqual(0);
      expect(p.when).toBeLessThanOrEqual(0.2);
    }
  });
});
