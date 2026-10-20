// What the player plays against: a plain clock (backing track only), an uploaded recording,
// or a YouTube video. All three answer "what time is it in the song?" smoothly.

export interface MediaAdapter {
  readonly kind: "none" | "audio" | "youtube";
  /** current media time in seconds, smooth between the element's coarse updates */
  time(): number;
  play(): Promise<void>;
  pause(): void;
  seek(t: number): void;
  setRate(rate: number): number; // returns the rate actually used
  isPlaying(): boolean;
  duration(): number | null;
  /** speeds this source supports (null = any 0.5..1.5) */
  rates(): number[] | null;
  /** about to play (the count-in has started): load what is needed to start at once */
  prepare?(): void;
  destroy(): void;
  onEnded?: () => void;
  /** the source started / stopped on its own (e.g. the YouTube player's own controls) */
  onExternalPlayState?: (playing: boolean) => void;
  onError?: (message: string) => void;
}

/** No recording: time just runs. */
export class ClockMedia implements MediaAdapter {
  readonly kind = "none" as const;
  private base = 0;
  private start = 0;
  private rate = 1;
  private playing = false;
  constructor(private readonly length: () => number) {}
  time() {
    return this.playing ? this.base + ((performance.now() - this.start) / 1000) * this.rate : this.base;
  }
  async play() {
    this.start = performance.now();
    this.playing = true;
  }
  pause() {
    this.base = this.time();
    this.playing = false;
  }
  seek(t: number) {
    this.base = t;
    this.start = performance.now();
  }
  setRate(r: number) {
    this.base = this.time();
    this.start = performance.now();
    this.rate = r;
    return r;
  }
  isPlaying() {
    return this.playing;
  }
  duration() {
    return this.length();
  }
  rates() {
    return null;
  }
  destroy() {
    this.playing = false;
  }
}

/** Smooths a coarse media clock: extrapolates between updates, never runs backwards by noise. */
class Smoother {
  private lastRaw = -1;
  private lastPerf = 0;
  private lastOut = 0;
  read(raw: number, playing: boolean, rate: number): number {
    const now = performance.now();
    if (!playing) {
      this.lastRaw = raw;
      this.lastPerf = now;
      this.lastOut = raw;
      return raw;
    }
    if (raw !== this.lastRaw) {
      this.lastRaw = raw;
      this.lastPerf = now;
    }
    let t = this.lastRaw + Math.min(0.3, ((now - this.lastPerf) / 1000) * rate);
    // tiny backwards steps are jitter; real seeks are large
    if (t < this.lastOut && this.lastOut - t < 0.08) t = this.lastOut;
    this.lastOut = t;
    return t;
  }
  reset() {
    this.lastRaw = -1;
  }
}

/** An uploaded recording, played by an <audio> element (time-stretched without pitch change). */
export class AudioMedia implements MediaAdapter {
  readonly kind = "audio" as const;
  readonly el: HTMLAudioElement;
  private smooth = new Smoother();
  onEnded?: () => void;
  onExternalPlayState?: (playing: boolean) => void;
  onError?: (message: string) => void;

  constructor(url: string) {
    this.el = new Audio();
    this.el.preload = "auto";
    this.el.src = url;
    this.el.preservesPitch = true;
    this.el.addEventListener("ended", () => this.onEnded?.());
    this.el.addEventListener("error", () => this.onError?.("This recording could not be loaded."));
  }
  time() {
    return this.smooth.read(this.el.currentTime, !this.el.paused, this.el.playbackRate);
  }
  async play() {
    try {
      await this.el.play();
    } catch (e) {
      this.onError?.("Playback was blocked. Press play again.");
      throw e;
    }
  }
  pause() {
    this.el.pause();
  }
  seek(t: number) {
    this.smooth.reset();
    this.el.currentTime = Math.max(0, t);
  }
  setRate(r: number) {
    this.el.playbackRate = r;
    return r;
  }
  isPlaying() {
    return !this.el.paused;
  }
  duration() {
    return Number.isFinite(this.el.duration) ? this.el.duration : null;
  }
  rates() {
    return null;
  }
  destroy() {
    this.el.pause();
    this.el.removeAttribute("src");
    this.el.load();
  }
}

// ---- YouTube ----

interface YTPlayer {
  playVideo(): void;
  pauseVideo(): void;
  seekTo(s: number, allowSeekAhead: boolean): void;
  getCurrentTime(): number;
  getDuration(): number;
  getPlayerState(): number;
  setPlaybackRate(r: number): void;
  getAvailablePlaybackRates(): number[];
  mute(): void;
  unMute(): void;
  destroy(): void;
}
declare global {
  interface Window {
    YT?: { Player: new (el: HTMLElement, opts: unknown) => YTPlayer; PlayerState: Record<string, number> };
    onYouTubeIframeAPIReady?: () => void;
  }
}

let ytApi: Promise<void> | null = null;
function loadYouTubeApi(): Promise<void> {
  if (window.YT?.Player) return Promise.resolve();
  if (!ytApi) {
    ytApi = new Promise((resolve, reject) => {
      const prev = window.onYouTubeIframeAPIReady;
      window.onYouTubeIframeAPIReady = () => {
        prev?.();
        resolve();
      };
      const s = document.createElement("script");
      s.src = "https://www.youtube.com/iframe_api";
      s.onerror = () => reject(new Error("YouTube could not be loaded."));
      document.head.appendChild(s);
    });
  }
  return ytApi;
}

export class YouTubeMedia implements MediaAdapter {
  /** how long to wait for silent playing to start before giving up on it */
  static primeWaitMs = 3000;
  readonly kind = "youtube" as const;
  /** set only once YouTube says the player is ready: before that its methods don't exist yet */
  private player: YTPlayer | null = null;
  private readonly box: HTMLDivElement;
  private destroyed = false;
  private smooth = new Smoother();
  private rate = 1;
  private wantPlay = false;
  /** YouTube starts a video that hasn't played yet when it is seeked, so until then a seek is
   *  only remembered, and made when playing starts */
  private pendingSeek: number | null = null;
  /** playing silently once to load the video (see prime) */
  private priming = false;
  private primeAt = 0;
  private primeTimer: ReturnType<typeof setTimeout> | undefined;
  readonly ready: Promise<void>;
  onEnded?: () => void;
  onExternalPlayState?: (playing: boolean) => void;
  onError?: (message: string) => void;

  constructor(host: HTMLElement, videoId: string) {
    // YouTube swaps the mount for its iframe; the box around it lets destroy() remove it for sure
    this.box = document.createElement("div");
    this.box.style.cssText = "width:100%;height:100%";
    const mount = document.createElement("div");
    this.box.appendChild(mount);
    host.appendChild(this.box);
    this.ready = loadYouTubeApi().then(
      () =>
        new Promise<void>((resolve) => {
          if (this.destroyed) return resolve();
          const p = new window.YT!.Player(mount, {
            videoId,
            width: "100%",
            height: "100%",
            playerVars: { playsinline: 1, rel: 0, modestbranding: 1, enablejsapi: 1, autoplay: 0, origin: location.origin },
            events: {
              onReady: () => {
                if (this.destroyed) {
                  safely(() => p.destroy());
                  return resolve();
                }
                this.player = p;
                if (this.rate !== 1) p.setPlaybackRate(this.rate);
                this.prime(p);
                resolve();
              },
              onStateChange: (e: { data: number }) => {
                if (this.destroyed) return;
                if (this.priming) {
                  // loaded: wait, paused, at the app's spot
                  if (e.data === 1 && !this.wantPlay) {
                    this.endPrime();
                    safely(() => p.pauseVideo());
                    if (this.pendingSeek === this.primeAt) this.pendingSeek = null;
                    else this.seekPending();
                  }
                  return;
                }
                // 1 playing, 2 paused, 0 ended
                if (e.data === 0) this.onEnded?.();
                // started from the video itself: go to where the app is
                if (e.data === 1) this.seekPending();
                if (e.data === 1 && !this.wantPlay) this.onExternalPlayState?.(true);
                if (e.data === 2 && this.wantPlay) this.onExternalPlayState?.(false);
                if (e.data === 1) this.wantPlay = true;
                if (e.data === 2) this.wantPlay = false;
              },
              onError: () => this.onError?.("This YouTube video can't be played here (it may be private or not allow embedding)."),
            },
          });
        }),
    );
    this.ready.catch((e) => this.onError?.(String(e?.message ?? e)));
  }
  /**
   * YouTube loads a video only once it plays, and a video that hasn't played yet starts when it
   * is moved. So it is started silently at the app's spot and paused straight away: from then on
   * it waits there, loaded, and starts the moment it is asked to (no wait after the count-in).
   * Done when the song opens, and again when Play is pressed (the count-in gives it time) in
   * case the browser didn't allow it unasked. Until it has played, the spot is kept for Play.
   */
  private prime(p: YTPlayer) {
    if (this.wantPlay || this.priming) return;
    const state = p.getPlayerState();
    if (state === 1 || state === 2 || state === 3) return; // already loaded
    this.priming = true;
    this.primeAt = Math.max(0, this.pendingSeek ?? 0);
    this.pendingSeek = this.primeAt;
    safely(() => {
      p.mute();
      p.seekTo(this.primeAt, true); // starts it, there
    });
    this.primeTimer = setTimeout(() => {
      if (!this.priming) return;
      this.endPrime();
      if (!this.wantPlay) safely(() => p.pauseVideo());
    }, YouTubeMedia.primeWaitMs);
  }
  prepare() {
    if (this.player && !this.destroyed) this.prime(this.player);
  }
  private endPrime() {
    if (!this.priming) return;
    this.priming = false;
    clearTimeout(this.primeTimer);
    safely(() => this.player?.unMute());
  }
  private playing() {
    return this.player?.getPlayerState() === 1;
  }
  private seekPending() {
    if (!this.player || this.pendingSeek === null) return;
    const t = this.pendingSeek;
    this.pendingSeek = null;
    this.smooth.reset();
    this.player.seekTo(Math.max(0, t), true);
  }
  time() {
    if (!this.player || this.pendingSeek !== null) return this.pendingSeek ?? 0;
    return this.smooth.read(this.player.getCurrentTime() ?? 0, this.playing(), this.rate);
  }
  async play() {
    this.wantPlay = true;
    await this.ready;
    if (this.destroyed || !this.wantPlay) return;
    this.endPrime();
    this.seekPending();
    this.player?.playVideo();
  }
  pause() {
    this.wantPlay = false;
    this.player?.pauseVideo();
  }
  seek(t: number) {
    this.smooth.reset();
    // seeking is safe while playing (1), paused (2) or buffering (3), or when about to play
    const state = this.player?.getPlayerState();
    if (!this.player || (!this.wantPlay && state !== 1 && state !== 2 && state !== 3)) {
      this.pendingSeek = t;
      return;
    }
    this.pendingSeek = null;
    this.player.seekTo(Math.max(0, t), true);
  }
  setRate(r: number) {
    const avail = this.rates();
    const best = avail.reduce((a, b) => (Math.abs(b - r) < Math.abs(a - r) ? b : a), 1);
    this.rate = best;
    this.player?.setPlaybackRate(best);
    return best;
  }
  isPlaying() {
    return this.wantPlay;
  }
  duration() {
    const d = this.player?.getDuration();
    return d ? d : null;
  }
  rates() {
    return (this.player?.getAvailablePlaybackRates() ?? [0.25, 0.5, 0.75, 1, 1.25, 1.5]).filter((r) => r >= 0.5 && r <= 1.5);
  }
  destroy() {
    this.destroyed = true;
    this.wantPlay = false;
    clearTimeout(this.primeTimer);
    const p = this.player;
    this.player = null;
    if (p) {
      safely(() => p.pauseVideo());
      safely(() => p.destroy());
    }
    // the iframe goes with the box, so nothing keeps playing in the background
    this.box.remove();
  }
}

function safely(fn: () => void) {
  try {
    fn();
  } catch {
    /* the player is already gone */
  }
}
