import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { YouTubeMedia } from "../../src/player/media";

interface Events {
  onReady(): void;
  onStateChange(e: { data: number }): void;
}

const players: FakePlayer[] = [];
/** whether the browser lets a muted video play without a tap */
let silentPlayAllowed = true;
/** whether the browser lets a video play with sound when asked by the page (an iPhone: only once
 *  the video itself has been tapped), and what the player does when it isn't */
let soundPlayAllowed = true;
let refusedState: number | null = null;

/** Behaves like YouTube's player: seeking a video that hasn't played yet (or has ended) starts it. */
class FakePlayer {
  state = -1; // unstarted
  time = 0;
  seeks: number[] = [];
  plays = 0;
  muted = false;
  tapped = false;
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
    if (!this.muted && !soundPlayAllowed && !this.tapped) {
      if (refusedState !== null) this.setState(refusedState);
      return;
    }
    this.plays++;
    this.setState(1);
  }
  /** a tap on the video itself */
  tap() {
    this.tapped = true;
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
  soundPlayAllowed = true;
  refusedState = null;
  YouTubeMedia.primeWaitMs = 3000;
  YouTubeMedia.startWaitMs = 2500;
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

describe("the end of a YouTube video", () => {
  it("goes back to the start without playing, then plays from there", async () => {
    const { media, player } = await open(0);
    const ended = vi.fn();
    media.onEnded = ended;
    await media.play();
    player.time = 200;
    player.state = 0;
    player.events.onStateChange({ data: 0 });
    expect(ended).toHaveBeenCalled();
    // the engine pauses and goes back to the start; pausing an ended video leaves it ended
    media.pause();
    player.state = 0;
    const seeks = player.seeks.length;
    media.seek(0);
    expect(player.seeks).toHaveLength(seeks); // moving an ended video would start it
    expect(player.state).toBe(0);
    expect(media.time()).toBe(0);
    await media.play();
    expect(player.seeks.at(-1)).toBe(0);
    expect(player.state).toBe(1);
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

describe("YouTube video on a phone that starts a video only from a tap on it (an iPhone)", () => {
  beforeEach(() => {
    soundPlayAllowed = false;
    YouTubeMedia.startWaitMs = 5;
  });
  const settle = () => new Promise((r) => setTimeout(r, 30));

  for (const [how, state] of [["stays as it was", null], ["pauses itself", 2]] as const) {
    it(`says Play was refused when the video ${how}, and follows a tap on the video`, async () => {
      refusedState = state;
      const { media, player, external } = await open(12);
      const blocked = vi.fn();
      media.onBlocked = blocked;
      await media.play(); // the count-in is over
      await settle();
      expect(blocked).toHaveBeenCalledTimes(1);
      expect(external).not.toHaveBeenCalled(); // not taken for the player pausing it
      expect(media.isPlaying()).toBe(false);
      // the player taps the video: it starts, at the app's spot, and the app follows
      player.tap();
      expect(external).toHaveBeenCalledWith(true);
      expect(player.state).toBe(1);
      // from then on Play works
      media.pause();
      await media.play();
      await settle();
      expect(player.state).toBe(1);
      expect(blocked).toHaveBeenCalledTimes(1);
    });
  }

  it("doesn't say so where Play starts the video, or while it is still loading", async () => {
    soundPlayAllowed = true;
    const { media, player } = await open();
    const blocked = vi.fn();
    media.onBlocked = blocked;
    await media.play();
    await settle();
    expect(blocked).not.toHaveBeenCalled();
    // a slow connection: loading (3) for a while, then playing
    media.pause();
    player.playVideo = function () {
      this.state = 3;
      this.events.onStateChange({ data: 3 });
    };
    await media.play();
    await settle();
    expect(blocked).not.toHaveBeenCalled();
    player.state = 1;
    player.events.onStateChange({ data: 1 });
    await settle();
    expect(blocked).not.toHaveBeenCalled();
  });
});

describe("YouTube video on an iPhone: Play starts it in the tap itself", () => {
  const store = new Map<string, string>();
  beforeEach(() => {
    store.clear();
    vi.stubGlobal("localStorage", { getItem: (k: string) => store.get(k) ?? null, setItem: (k: string, v: string) => store.set(k, v) });
  });

  it("asks a ready player before anything waits", async () => {
    const { media, player } = await open(12);
    void media.play(); // not awaited: still inside the tap
    expect(player.state).toBe(1);
  });

  it("starts in the tap on an iPhone, and anywhere that has refused once", async () => {
    vi.stubGlobal("navigator", { userAgent: "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/140" });
    const { media, player } = await open();
    expect(media.startsOnlyInTap()).toBe(false);
    vi.stubGlobal("navigator", { userAgent: "Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X) CriOS/140 Mobile/15E148 Safari/604.1" });
    expect(media.startsOnlyInTap()).toBe(true);
    // a computer that refused once: from then on, in the tap too
    vi.stubGlobal("navigator", { userAgent: "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/140" });
    soundPlayAllowed = false;
    YouTubeMedia.startWaitMs = 5;
    await media.play();
    await new Promise((r) => setTimeout(r, 30));
    expect(player.state).not.toBe(1);
    expect(media.startsOnlyInTap()).toBe(true);
  });
});
