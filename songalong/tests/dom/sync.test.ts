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

const store = new Map<string, string>();

beforeEach(() => {
  store.clear();
  vi.stubGlobal("window", { setInterval: () => 0, clearInterval() {}, setTimeout: () => 0 });
  vi.stubGlobal("requestAnimationFrame", () => 0);
  vi.stubGlobal("cancelAnimationFrame", () => {});
  vi.stubGlobal("localStorage", { getItem: (k: string) => store.get(k) ?? null, setItem: (k: string, v: string) => store.set(k, v) });
});

afterEach(() => {
  vi.unstubAllGlobals();
});

async function playing(sync: number) {
  const engine = new PlayerEngine(DEMO_SONGS[0].doc);
  const media = new FakeMedia();
  engine.setMedia(media);
  engine.setCountIn(0);
  engine.setSync(sync);
  engine.seekTime(10);
  await engine.play();
  return { engine, media };
}

describe("sync with what is heard", () => {
  it("shows the song where the player is when there is no delay", async () => {
    const { engine, media } = await playing(0);
    media.now = 12;
    expect(engine.heardTime()).toBe(12);
  });

  it("waits at the starting spot, then trails the player by the delay", async () => {
    const { engine, media } = await playing(0.3);
    media.now = 10.2;
    expect(engine.heardTime()).toBe(10);
    media.now = 12;
    expect(engine.heardTime()).toBeCloseTo(11.7);
    // moved while playing: waits there again
    engine.seekTime(40);
    media.now = 40.1;
    expect(engine.heardTime()).toBe(40);
  });

  it("counts the delay in real seconds at any speed", async () => {
    const { engine, media } = await playing(0.3);
    engine.setSpeed(0.5);
    media.now = 12;
    expect(engine.heardTime()).toBeCloseTo(11.85);
  });

  it("shows the player's spot when paused", async () => {
    const { engine, media } = await playing(0.3);
    media.now = 12;
    engine.pause();
    expect(engine.heardTime()).toBe(12);
  });

  it("is remembered on this device, within limits", async () => {
    await playing(0.35);
    expect(store.get("sa-sync")).toBe("0.35");
    expect(new PlayerEngine(DEMO_SONGS[0].doc).getState().sync).toBe(0.35);
    const { engine } = await playing(5);
    expect(engine.getState().sync).toBe(1);
  });
});
