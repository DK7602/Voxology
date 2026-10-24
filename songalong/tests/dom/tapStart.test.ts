import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

const clicks: number[] = [];
vi.mock("../../src/player/synth", () => ({
  audioContext: () => ({ currentTime: 0 }),
  unlockAudio: async () => {},
  click: (when: number) => clicks.push(when),
  strum() {},
  arpeggio() {},
}));

import { DEMO_SONGS } from "../../shared/demoSongs";
import { PlayerEngine } from "../../src/player/engine";
import { FakeMedia } from "./fakeMedia";

/** A video that starts only in the tap that asks it (YouTube on an iPhone). */
class TapOnlyVideo extends FakeMedia {
  startsOnlyInTap() {
    return true;
  }
}

beforeEach(() => {
  clicks.length = 0;
  vi.stubGlobal("window", { setInterval: () => 0, clearInterval() {}, setTimeout: () => 0 });
  vi.stubGlobal("requestAnimationFrame", () => 0);
  vi.stubGlobal("cancelAnimationFrame", () => {});
});

afterEach(() => {
  vi.unstubAllGlobals();
});

function engineWith(media: FakeMedia) {
  const engine = new PlayerEngine(DEMO_SONGS[0].doc);
  engine.setMedia(media);
  engine.setCountIn(1);
  return engine;
}
const schedule = (engine: PlayerEngine) => (engine as unknown as { schedule(): void }).schedule();

describe("Play on an iPhone (a video that starts only in the tap)", () => {
  it("starts the video in the tap, a bar early, and counts in over that bar on the video's clock", () => {
    const media = new TapOnlyVideo();
    const engine = engineWith(media);
    engine.seekBeat(8); // bar 3
    void engine.play(); // still inside the tap
    expect(media.playing).toBe(true);
    expect(engine.timeline.timeToBeat(media.now)).toBeCloseTo(4); // a bar early
    expect(engine.getState().countIn).toBe(4);
    // the video plays the bar before: the clicks count it, 4, 3, 2, 1, then the song goes on
    for (let beat = 4; beat <= 8.5; beat += 0.25) {
      media.now = engine.timeline.beatToTime(beat);
      schedule(engine);
      if (beat === 6) expect(engine.getState().countIn).toBe(2);
    }
    expect(clicks).toHaveLength(4);
    expect(engine.getState().countIn).toBeNull();
    expect(engine.getState().playing).toBe(true);
  });

  it("starts right there at the very start of a song (no bar before it)", () => {
    const media = new TapOnlyVideo();
    const engine = engineWith(media);
    engine.seekTime(0);
    void engine.play();
    expect(media.playing).toBe(true);
    expect(media.now).toBe(0);
    expect(engine.getState().countIn).toBeNull();
  });

  it("counts in the usual way where Play may start the video later", async () => {
    const media = new FakeMedia();
    const engine = engineWith(media);
    engine.seekBeat(8);
    await engine.play();
    expect(media.playing).toBe(false); // it starts when the count-in is over
    expect(engine.getState().countIn).toBe(4);
  });
});
