import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { YouTubeMedia } from "../../src/player/media";

interface Events {
  onReady(): void;
  onStateChange(e: { data: number }): void;
}

const players: FakePlayer[] = [];
/** whether the browser lets a muted video play without a tap */
let silentPlayAllowed = true;

/** Behaves like YouTube's player: seeking a video that hasn't played yet (or has ended) starts it. */
class FakePlayer {
  state = -1; // unstarted
  time = 0;
  seeks: number[] = [];
  plays = 0;
  muted = false;
  readonly events: Events;
  constructor(_mount: unknown, opts: { events: Events }) {
    this.events = opts.events;
    players.push(this);
  }
  private setState(s: number) {
    this.state = s;
    this.events.onStateChange({ data: s });
  }
  playVideo() {
    if (this.muted && !silentPlayAllowed && this.state !== 2) return;
    this.plays++;
    this.setState(1);
  }
  mute() {
    this.muted = true;
  }
  unMute() {
    this.muted = false;
  }
  pauseVideo() {
    this.setState(2);
  }
  seekTo(t: number) {
    this.seeks.push(t);
    this.time = t;
    if (this.state === -1 || this.state === 5 || this.state === 0) {
      if (this.muted && !silentPlayAllowed) return;
      this.setState(1);
    }
  }
  getCurrentTime() {
    return this.time;
  }
  getDuration() {
    return 200;
  }
  getPlayerState() {
    return this.state;
  }
  setPlaybackRate() {}
  getAvailablePlaybackRates() {
    return [0.5, 1, 1.5];
  }
  destroy() {}
}

const element = () => ({ style: { cssText: "" }, appendChild() {}, remove() {} });

beforeEach(() => {
  players.length = 0;
  silentPlayAllowed = true;
  YouTubeMedia.primeWaitMs = 3000;
  vi.stubGlobal("window", { YT: { Player: FakePlayer, PlayerState: {} } });
  vi.stubGlobal("document", { createElement: element, head: { appendChild() {} } });
  vi.stubGlobal("location", { origin: "https://songalong.test" });
});

afterEach(() => {
  vi.unstubAllGlobals();
});

/** Opens a video the way the song page does (the engine seeks to the start), and gets it ready. */
async function open(startAt = 0) {
  const media = new YouTubeMedia(element() as never, "abc123");
  const external = vi.fn();
  media.onExternalPlayState = external;
  media.seek(startAt);
  await vi.waitFor(() => expect(players).toHaveLength(1));
  const player = players[0];
  player.events.onReady();
  await media.ready;
  return { media, player, external };
}

describe("YouTube video", () => {
  it("loads silently when the song opens and waits, paused, at the app's spot", async () => {
    const { media, player, external } = await open(12);
    expect(player.state).toBe(2);
    expect(player.muted).toBe(false);
    expect(player.seeks).toEqual([12]);
    expect(external).not.toHaveBeenCalled();
    expect(media.isPlaying()).toBe(false);
    media.seek(42); // tap a lyric line: the paused video moves there (and loads it)
    expect(player.seeks).toEqual([12, 42]);
    expect(player.state).toBe(2);
    expect(media.time()).toBe(42);
  });

  it("starts at once when play is pressed (after the count-in)", async () => {
    const { media, player, external } = await open(12);
    media.seek(20); // the count-in starts on a whole beat
    await media.play();
    expect(player.state).toBe(1);
    expect(player.seeks).toEqual([12, 20]);
    expect(player.muted).toBe(false);
    expect(external).not.toHaveBeenCalled();
  });
});

describe("YouTube video where silent playing isn't allowed", () => {
  beforeEach(() => {
    silentPlayAllowed = false;
  });

  it("doesn't start when the song opens or when a line is tapped before playing", async () => {
    const { media, player, external } = await open();
    media.seek(42); // tap a lyric line
    expect(player.state).toBe(-1);
    expect(player.seeks).toEqual([0]); // the silent start it wasn't allowed
    expect(player.plays).toBe(0);
    expect(external).not.toHaveBeenCalled();
    expect(media.isPlaying()).toBe(false);
    expect(media.time()).toBe(42);
  });

  it("goes to the remembered spot when play is pressed", async () => {
    const { media, player, external } = await open();
    media.seek(42);
    await media.play();
    expect(player.seeks).toEqual([0, 42]);
    expect(player.plays).toBe(1);
    expect(player.state).toBe(1);
    expect(player.muted).toBe(false);
    expect(external).not.toHaveBeenCalled();
    expect(media.time()).toBe(42);
  });

  it("loads during the count-in, once Play has been pressed", async () => {
    YouTubeMedia.primeWaitMs = 5;
    const { media, player } = await open();
    await new Promise((r) => setTimeout(r, 20)); // gave up on loading unasked
    media.seek(20); // Play pressed: the count-in starts here
    silentPlayAllowed = true; // the tap allows it
    media.prepare();
    expect(player.state).toBe(2); // loaded, waiting
    expect(player.muted).toBe(false);
    expect(player.seeks.at(-1)).toBe(20);
    await media.play(); // the count-in is over
    expect(player.state).toBe(1);
    expect(player.seeks.at(-1)).toBe(20);
  });

  it("seeks at once while paused, and stays paused", async () => {
    const { media, player } = await open();
    await media.play();
    media.pause();
    media.seek(10);
    expect(player.seeks.at(-1)).toBe(10);
    expect(player.state).toBe(2);
    expect(media.time()).toBe(10);
  });

  it("keeps the app's spot when play is pressed on the video itself", async () => {
    YouTubeMedia.primeWaitMs = 5;
    const { player, external } = await open(30);
    await new Promise((r) => setTimeout(r, 20)); // it gives up on silent playing
    expect(player.muted).toBe(false);
    player.playVideo();
    expect(player.seeks.at(-1)).toBe(30);
    expect(player.time).toBe(30);
    expect(external).toHaveBeenCalledWith(true);
  });
});
