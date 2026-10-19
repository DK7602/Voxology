import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { YouTubeMedia } from "../../src/player/media";

interface Events {
  onReady(): void;
  onStateChange(e: { data: number }): void;
}

const players: FakePlayer[] = [];

/** Behaves like YouTube's player: seeking a video that hasn't played yet (or has ended) starts it. */
class FakePlayer {
  state = -1; // unstarted
  time = 0;
  seeks: number[] = [];
  plays = 0;
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
    this.plays++;
    this.setState(1);
  }
  pauseVideo() {
    this.setState(2);
  }
  seekTo(t: number) {
    this.seeks.push(t);
    this.time = t;
    if (this.state === -1 || this.state === 5 || this.state === 0) this.setState(1);
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
  it("doesn't start when the song opens or when a line is tapped before playing", async () => {
    const { media, player, external } = await open();
    media.seek(42); // tap a lyric line
    expect(player.state).toBe(-1);
    expect(player.seeks).toEqual([]);
    expect(player.plays).toBe(0);
    expect(external).not.toHaveBeenCalled();
    expect(media.isPlaying()).toBe(false);
    expect(media.time()).toBe(42);
  });

  it("goes to the remembered spot when play is pressed", async () => {
    const { media, player, external } = await open();
    media.seek(42);
    await media.play();
    expect(player.seeks).toEqual([42]);
    expect(player.plays).toBe(1);
    expect(player.state).toBe(1);
    expect(external).not.toHaveBeenCalled();
    expect(media.time()).toBe(42);
  });

  it("seeks at once while paused, and stays paused", async () => {
    const { media, player } = await open();
    await media.play();
    media.pause();
    media.seek(10);
    expect(player.seeks).toEqual([0, 10]);
    expect(player.state).toBe(2);
    expect(media.time()).toBe(10);
  });

  it("keeps the app's spot when play is pressed on the video itself", async () => {
    const { player, external } = await open(30);
    player.playVideo();
    expect(player.seeks).toEqual([30]);
    expect(external).toHaveBeenCalledWith(true);
  });
});
