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
  readonly kind = "youtube" as const;
  private player: YTPlayer | null = null;
  private smooth = new Smoother();
  private rate = 1;
  private wantPlay = false;
  private pendingSeek: number | null = null;
  readonly ready: Promise<void>;
  onEnded?: () => void;
  onExternalPlayState?: (playing: boolean) => void;
  onError?: (message: string) => void;

  constructor(host: HTMLElement, videoId: string) {
    const mount = document.createElement("div");
    host.appendChild(mount);
    this.ready = loadYouTubeApi().then(
      () =>
        new Promise<void>((resolve) => {
          this.player = new window.YT!.Player(mount, {
            videoId,
            width: "100%",
            height: "100%",
            playerVars: { playsinline: 1, rel: 0, modestbranding: 1, enablejsapi: 1, origin: location.origin },
            events: {
              onReady: () => {
                if (this.pendingSeek !== null) this.player!.seekTo(this.pendingSeek, true);
                resolve();
              },
              onStateChange: (e: { data: number }) => {
                // 1 playing, 2 paused, 0 ended
                if (e.data === 0) this.onEnded?.();
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
  private playing() {
    return this.player?.getPlayerState() === 1;
  }
  time() {
    if (!this.player) return this.pendingSeek ?? 0;
    return this.smooth.read(this.player.getCurrentTime() ?? 0, this.playing(), this.rate);
  }
  async play() {
    this.wantPlay = true;
    await this.ready;
    this.player!.playVideo();
  }
  pause() {
    this.wantPlay = false;
    this.player?.pauseVideo();
  }
  seek(t: number) {
    this.smooth.reset();
    if (!this.player) this.pendingSeek = t;
    else this.player.seekTo(Math.max(0, t), true);
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
    try {
      this.player?.destroy();
    } catch {
      /* already gone */
    }
  }
}
