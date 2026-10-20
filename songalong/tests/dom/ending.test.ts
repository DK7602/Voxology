import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

vi.mock("../../src/player/synth", () => ({
  audioContext: () => ({ currentTime: 0 }),
  unlockAudio: async () => {},
  click() {},
  strum() {},
  arpeggio() {},
}));

import { DEMO_SONGS } from "../../shared/demoSongs";
import { PlayerEngine } from "../../src/player/engine";
import { FakeMedia } from "./fakeMedia";

beforeEach(() => {
  vi.stubGlobal("window", { setInterval: () => 0, clearInterval() {}, setTimeout: () => 0 });
  vi.stubGlobal("requestAnimationFrame", () => 0);
  vi.stubGlobal("cancelAnimationFrame", () => {});
});

afterEach(() => {
  vi.unstubAllGlobals();
});

async function playingFrom(t: number) {
  const engine = new PlayerEngine(DEMO_SONGS[0].doc);
  const media = new FakeMedia();
  engine.setMedia(media);
  engine.setCountIn(0);
  engine.seekTime(t);
  await engine.play();
  return { engine, media };
}

describe("the end of a recording", () => {
  it("stops and goes back to the start, ready to play again", async () => {
    const { engine, media } = await playingFrom(150);
    media.now = 200;
    media.onEnded!();
    expect(engine.getState().playing).toBe(false);
    expect(media.now).toBe(0);
  });

  it("starts the loop again when looping", async () => {
    const { engine, media } = await playingFrom(150);
    engine.setLoop({ on: true });
    media.now = 200;
    media.onEnded!();
    expect(media.now).toBe(engine.timeline.beatToTime(0));
    expect(engine.getState().playing).toBe(true);
  });
});
