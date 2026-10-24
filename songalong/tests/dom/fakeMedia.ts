import type { MediaAdapter } from "../../src/player/media";

/** A recording whose clock the test moves by hand. */
export class FakeMedia implements MediaAdapter {
  readonly kind = "audio" as const;
  now = 0;
  playing = false;
  time() {
    return this.now;
  }
  async play() {
    this.playing = true;
  }
  pause() {
    this.playing = false;
  }
  seek(t: number) {
    this.now = t;
  }
  setRate(r: number) {
    return r;
  }
  isPlaying() {
    return this.playing;
  }
  duration() {
    return 200;
  }
  rates() {
    return null;
  }
  unlocked = 0;
  unlock() {
    this.unlocked++;
  }
  destroy() {}
  onEnded?: () => void;
  onExternalPlayState?: (playing: boolean) => void;
  onBlocked?: () => void;
}

