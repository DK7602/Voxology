// The play-along engine: drives the media, counts in, loops, plays the backing chords and
// metronome, and tells the UI where in the song we are (every animation frame).
import type { ChordEvent, SongDoc } from "../../shared/types";
import { Timeline, chordEnd, indexAt } from "../../shared/timeline";
import { ClockMedia, type MediaAdapter } from "./media";
import { clampSync, savedSync, saveSync } from "./sync";
import { arpeggio, audioContext, cajonHit, click, strum, unlockAudio, type CajonStroke } from "./synth";
import type { Instrument } from "../../shared/types";

export interface LoopRange {
  on: boolean;
  /** beats; undefined = the whole song */
  from?: number;
  to?: number;
}

export interface PlayerState {
  playing: boolean;
  /** count-in in progress: beats left to count (shown as 4, 3, 2, 1) */
  countIn: number | null;
  speed: number;
  loop: LoopRange;
  /** play the rhythm part (chords, or the cajon part on the cajon) with the built-in instrument */
  backing: boolean;
  /** play the lead part as a guide */
  leadGuide: boolean;
  metronome: boolean;
  /** bars of count-in before playing (0 = off) */
  countInBars: number;
  /** seconds later than the media clock that words and chords are shown (sync.ts) */
  sync: number;
  error: string | null;
  mediaKind: MediaAdapter["kind"];
}

export type Part = "rhythm" | "lead";

/** Notes to sound for event i of a part: a chord (strummed) or a riff (played in order). */
export type ChordVoicer = (part: Part, index: number) => { midis: number[]; riff: boolean } | null;

type Tick = (time: number, beat: number) => void;

export class PlayerEngine {
  timeline: Timeline;
  media: MediaAdapter;
  state: PlayerState;
  instrument: Instrument = "guitar";
  voicer: ChordVoicer = () => null;
  /** what the backing plays (the display may show either part) */
  private rhythm: ChordEvent[];
  private lead: ChordEvent[] = [];
  private stateListeners = new Set<() => void>();
  private tickListeners = new Set<Tick>();
  private raf = 0;
  private scheduler = 0;
  private scheduledUpTo = -Infinity; // beat index already scheduled
  private leadScheduledUpTo = -Infinity; // beat position of the last lead event scheduled
  /** the cajon part, in beat order: what the backing plays on the cajon */
  private cajon: { beat: number; stroke: CajonStroke }[] = [];
  private cajonScheduledUpTo = -Infinity; // beat position of the last cajon stroke scheduled
  private countTimers: number[] = [];
  private lastBeat = 0;
  /** where playing started (or was moved to): what is shown waits there for the sound */
  private shownFrom = 0;

  constructor(doc: SongDoc) {
    this.timeline = new Timeline(doc);
    this.rhythm = doc.chords;
    this.lead = doc.lead ?? [];
    this.media = new ClockMedia(() => this.timeline.duration);
    this.state = {
      playing: false,
      countIn: null,
      speed: 1,
      loop: { on: false },
      backing: doc.media.kind === "none",
      leadGuide: false,
      metronome: false,
      countInBars: 1,
      sync: savedSync(),
      error: null,
      mediaKind: "none",
    };
    this.wire(this.media);
    this.seekTime(this.startTime());
  }

  /** Where "the start" is: beat 0 for the backing track, the very beginning of a recording
   *  (which may have a few seconds before beat 0). */
  private startTime(): number {
    return this.media.kind === "none" ? Math.min(0, this.timeline.beatToTime(0)) : 0;
  }

  // ---- subscriptions ----
  subscribe = (fn: () => void) => {
    this.stateListeners.add(fn);
    return () => {
      this.stateListeners.delete(fn);
    };
  };
  getState = () => this.state;
  onTick(fn: Tick) {
    this.tickListeners.add(fn);
    fn(this.media.time(), this.lastBeat);
    return () => {
      this.tickListeners.delete(fn);
    };
  }
  private set(patch: Partial<PlayerState>) {
    this.state = { ...this.state, ...patch };
    this.stateListeners.forEach((f) => f());
  }

  // ---- media ----
  setMedia(media: MediaAdapter) {
    const wasAt = this.media.time();
    this.stop();
    this.media.destroy();
    this.media = media;
    this.wire(media);
    media.setRate(this.state.speed);
    media.seek(wasAt);
    this.set({ mediaKind: media.kind, error: null, backing: media.kind === "none" });
    this.emitTick();
  }
  private wire(m: MediaAdapter) {
    m.onEnded = () => {
      if (this.state.loop.on) this.seekBeat(this.state.loop.from ?? 0);
      else {
        // played to the end: ready to play again from the top
        this.pause();
        this.restart();
      }
    };
    m.onExternalPlayState = (playing) => {
      if (playing && !this.state.playing) this.startRunning();
      if (!playing && this.state.playing) this.pause();
    };
    m.onError = (message) => this.set({ error: message, playing: false });
  }

  /** The document to show (its `chords` may be the lead part). */
  setDoc(doc: SongDoc) {
    this.timeline = new Timeline(doc);
    this.resetSchedule();
    this.emitTick();
  }

  /** The parts the backing plays from. */
  setParts(rhythm: ChordEvent[], lead: ChordEvent[]) {
    this.rhythm = rhythm;
    this.lead = lead;
  }

  /** The cajon part (strokes in beat order), played instead of the chords on the cajon. */
  setCajon(strokes: { beat: number; stroke: CajonStroke }[]) {
    this.cajon = strokes;
    this.cajonScheduledUpTo = this.lastBeat - 1e-6;
  }

  private resetSchedule() {
    this.scheduledUpTo = Math.ceil(this.lastBeat - 1e-6) - 1;
    this.leadScheduledUpTo = this.lastBeat - 1e-6;
    this.cajonScheduledUpTo = this.lastBeat - 1e-6;
  }

  // ---- transport ----
  get beat() {
    return this.lastBeat;
  }

  async play() {
    if (this.state.playing || this.state.countIn !== null) return;
    await unlockAudio();
    this.set({ error: null });
    const bars = this.state.countInBars;
    if (bars > 0) {
      // start on a whole beat so the count lines up with the music
      const start = Math.floor(this.lastBeat + 1e-6);
      this.seekBeat(start);
      this.media.prepare?.();
      const beats = bars * (this.timeline.doc.beatsPerBar || 4);
      const spb = this.timeline.secondsPerBeat(Math.max(0, start)) / this.state.speed;
      const ctx = audioContext();
      const t0 = ctx.currentTime + 0.08;
      for (let i = 0; i < beats; i++) click(t0 + i * spb, i % (this.timeline.doc.beatsPerBar || 4) === 0, 0.75);
      this.set({ countIn: beats });
      for (let i = 1; i <= beats; i++) {
        const id = window.setTimeout(() => {
          if (i < beats) this.set({ countIn: beats - i });
          else {
            this.set({ countIn: null });
            this.startRunning(true);
          }
        }, (0.08 + i * spb) * 1000 - 25);
        this.countTimers.push(id);
      }
      this.loop();
      return;
    }
    this.startRunning(true);
  }

  private startRunning(startMedia = false) {
    this.shownFrom = this.media.time();
    this.resetSchedule();
    this.set({ playing: true });
    if (startMedia) {
      this.media.play().catch(() => this.set({ playing: false }));
    }
    window.clearInterval(this.scheduler);
    this.scheduler = window.setInterval(() => this.schedule(), 25);
    this.schedule();
    this.loop();
  }

  pause() {
    this.countTimers.forEach((t) => clearTimeout(t));
    this.countTimers = [];
    window.clearInterval(this.scheduler);
    this.media.pause();
    this.set({ playing: false, countIn: null });
    this.emitTick();
  }

  private stop() {
    this.pause();
    cancelAnimationFrame(this.raf);
  }

  toggle() {
    if (this.state.playing || this.state.countIn !== null) this.pause();
    else void this.play();
  }

  seekBeat(beat: number) {
    this.seekTime(this.timeline.beatToTime(beat));
  }

  seekTime(t: number) {
    const dur = this.media.duration() ?? this.timeline.duration;
    const clamped = Math.max(this.startTime(), Math.min(t, dur));
    this.media.seek(clamped);
    this.shownFrom = clamped;
    this.lastBeat = this.timeline.timeToBeat(clamped);
    this.resetSchedule();
    this.emitTick(clamped);
  }

  /** Jump to the start of the bar before / after the current one. */
  skipBars(n: number) {
    const bpb = this.timeline.doc.beatsPerBar || 4;
    const bar = Math.floor(this.lastBeat / bpb + 1e-6);
    const target = n < 0 && this.lastBeat - bar * bpb > 0.6 ? bar : bar + n;
    this.seekBeat(Math.max(0, target) * bpb);
  }

  restart() {
    if (this.state.loop.on && this.state.loop.from !== undefined) this.seekBeat(this.state.loop.from);
    else this.seekTime(this.startTime());
  }

  setSpeed(speed: number) {
    const used = this.media.setRate(Math.max(0.25, Math.min(2, speed)));
    this.set({ speed: used });
  }
  setLoop(loop: LoopRange) {
    this.set({ loop });
  }
  setBacking(on: boolean) {
    this.set({ backing: on });
  }
  setLeadGuide(on: boolean) {
    this.set({ leadGuide: on });
  }
  setMetronome(on: boolean) {
    this.set({ metronome: on });
  }
  setCountIn(bars: number) {
    this.set({ countInBars: bars });
  }
  /** Show words and chords this many seconds later (remembered on this device). */
  setSync(seconds: number) {
    const sync = clampSync(seconds);
    saveSync(sync);
    this.set({ sync });
  }

  /** Where in the song the sound being heard is: the media clock less the sync delay while
   *  playing (song seconds pass at the playing speed), never before where playing started. */
  heardTime(t = this.media.time()): number {
    const { playing, sync, speed } = this.state;
    if (!playing || !sync) return t;
    return sync > 0 ? Math.max(Math.min(t, this.shownFrom), t - sync * speed) : t - sync * speed;
  }

  destroy() {
    try {
      this.stop();
    } finally {
      // always let go of the media, so a video can't keep playing after the page closes
      this.media.destroy();
    }
    this.stateListeners.clear();
    this.tickListeners.clear();
  }

  // ---- frame loop ----
  private loop = () => {
    cancelAnimationFrame(this.raf);
    const step = () => {
      const t = this.media.time();
      const beat = this.timeline.timeToBeat(t);
      const { loop } = this.state;
      if (this.state.playing && loop.on) {
        const to = loop.to ?? this.timeline.lengthBeats;
        if (beat >= to) {
          this.seekBeat(loop.from ?? 0);
          this.raf = requestAnimationFrame(step);
          return;
        }
      }
      if (this.state.playing && this.media.kind === "none" && beat >= this.timeline.lengthBeats) {
        if (loop.on) this.seekBeat(loop.from ?? 0);
        else {
          this.pause();
          this.restart();
          return;
        }
      }
      // shown as heard: the loop and the end above go by the media itself
      const heard = this.heardTime(t);
      const shownBeat = heard === t ? beat : this.timeline.timeToBeat(heard);
      this.lastBeat = shownBeat;
      this.tickListeners.forEach((f) => f(heard, shownBeat));
      if (this.state.playing || this.state.countIn !== null) this.raf = requestAnimationFrame(step);
    };
    this.raf = requestAnimationFrame(step);
  };

  private emitTick(t = this.media.time()) {
    const beat = this.timeline.timeToBeat(t);
    this.lastBeat = beat;
    this.tickListeners.forEach((f) => f(t, beat));
  }

  // ---- backing track + metronome (look-ahead scheduling on the audio clock) ----
  private schedule() {
    if (!this.state.playing) return;
    const ctx = audioContext();
    const now = this.media.time();
    const nowBeat = this.timeline.timeToBeat(now);
    const rate = this.state.speed || 1;
    const horizonBeat = this.timeline.timeToBeat(now + 0.15 * rate);
    const doc = this.timeline.doc;
    const bpb = doc.beatsPerBar || 4;
    const loopTo = this.state.loop.on ? this.state.loop.to ?? this.timeline.lengthBeats : Infinity;
    for (let b = Math.max(this.scheduledUpTo + 1, Math.ceil(nowBeat - 0.02)); b <= horizonBeat; b++) {
      this.scheduledUpTo = b;
      if (b >= loopTo || b < 0 || b >= this.timeline.lengthBeats) continue;
      const when = ctx.currentTime + (this.timeline.beatToTime(b) - now) / rate;
      if (when < ctx.currentTime - 0.03) continue;
      const posInBar = ((b % bpb) + bpb) % bpb;
      if (this.state.metronome) click(when, posInBar === 0, 0.5);
      if (!this.state.backing || this.instrument === "cajon") continue;
      const rhythm = this.rhythm;
      const ci = indexAt(rhythm, b + 1e-6);
      if (ci < 0) continue;
      const chord = rhythm[ci];
      const v = this.voicer("rhythm", ci);
      if (!v || !v.midis.length) continue;
      const starts = Math.abs(chord.beat - b) < 1e-6;
      const end = chordEnd(rhythm, ci, this.timeline.lengthBeats);
      if (v.riff) {
        if (!starts) continue;
        const beats = Math.max(1, end - chord.beat);
        const step = (Math.min(beats, v.midis.length) / v.midis.length) * this.timeline.secondsPerBeat(b) / rate;
        arpeggio(v.midis, when, step, this.instrument);
        continue;
      }
      if (this.instrument === "bass") {
        // a simple bass line: the bass note on each chord change and on beats 1 and 3
        const hitEvery = bpb === 4 ? 2 : bpb;
        if (starts || posInBar % hitEvery === 0) {
          const next = Math.min(end, b + hitEvery - (posInBar % hitEvery));
          const until = ctx.currentTime + (this.timeline.beatToTime(next) - now) / rate - 0.02;
          strum([v.midis[0]], when, { instrument: "bass", velocity: starts || posInBar === 0 ? 0.95 : 0.75, until });
        }
        continue;
      }
      if (this.instrument === "piano") {
        if (starts || posInBar === 0) {
          const until = ctx.currentTime + (this.timeline.beatToTime(Math.min(end, b + bpb - posInBar)) - now) / rate;
          strum(v.midis, when, { instrument: "piano", velocity: starts ? 0.8 : 0.6, until });
        }
        continue;
      }
      // a gentle strum pattern: strong 1, lighter 2 & 4 (up-strum on 4)
      const pattern = bpb === 3 ? [0.85, 0.45, 0.45] : [0.85, 0.45, 0.65, 0.45];
      const vel = starts ? 0.85 : pattern[posInBar] ?? 0.45;
      const up = bpb === 4 && posInBar === 3 && !starts;
      const until = ctx.currentTime + (this.timeline.beatToTime(Math.min(end, b + 1)) - now) / rate + 0.05;
      strum(v.midis, when, { instrument: this.instrument, velocity: vel, down: !up, spread: up ? 0.012 : 0.02, until: end <= b + 1 ? until : undefined });
    }
    if (this.state.leadGuide) this.scheduleLead(now, horizonBeat, loopTo, rate);
    if (this.state.backing && this.instrument === "cajon") this.scheduleCajon(now, horizonBeat, loopTo, rate);
  }

  /** Cajon strokes fall on 16th notes, so they are scheduled by their own positions. */
  private scheduleCajon(now: number, horizonBeat: number, loopTo: number, rate: number) {
    const ctx = audioContext();
    const strokes = this.cajon;
    // the first stroke after the last one scheduled
    let lo = 0;
    let hi = strokes.length;
    while (lo < hi) {
      const mid = (lo + hi) >> 1;
      if (strokes[mid].beat <= this.cajonScheduledUpTo) lo = mid + 1;
      else hi = mid;
    }
    for (let i = lo; i < strokes.length && strokes[i].beat <= horizonBeat; i++) {
      const s = strokes[i];
      this.cajonScheduledUpTo = s.beat;
      if (s.beat >= loopTo || s.beat >= this.timeline.lengthBeats || s.beat < this.lastBeat - 0.1) continue;
      const when = ctx.currentTime + (this.timeline.beatToTime(s.beat) - now) / rate;
      if (when < ctx.currentTime - 0.03) continue;
      // the beats a little stronger than what falls between them
      const onBeat = Math.abs(s.beat - Math.round(s.beat)) < 1e-6;
      cajonHit(s.stroke, when, s.stroke === "t" ? 0.6 : onBeat ? 0.95 : 0.8);
    }
  }

  /** Lead notes fall between beats too, so they are scheduled by their own positions. */
  private scheduleLead(now: number, horizonBeat: number, loopTo: number, rate: number) {
    const ctx = audioContext();
    const lead = this.lead;
    let i = indexAt(lead, this.leadScheduledUpTo) + 1;
    for (; i < lead.length && lead[i].beat <= horizonBeat; i++) {
      const e = lead[i];
      this.leadScheduledUpTo = e.beat;
      if (e.beat >= loopTo || e.beat < this.lastBeat - 0.25) continue;
      const v = this.voicer("lead", i);
      if (!v || !v.midis.length) continue;
      const when = ctx.currentTime + (this.timeline.beatToTime(e.beat) - now) / rate;
      const endBeat = Math.min(chordEnd(lead, i, this.timeline.lengthBeats), e.beat + 4);
      const dur = (this.timeline.beatToTime(endBeat) - this.timeline.beatToTime(e.beat)) / rate;
      if (v.riff && v.midis.length > 1) arpeggio(v.midis, when, dur / v.midis.length, this.instrument);
      else strum(v.midis, when, { instrument: this.instrument, velocity: 0.75, spread: 0.01, until: when + dur });
    }
  }
}
