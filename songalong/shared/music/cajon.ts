// Cajon parts from a song's drums: kick -> Bass (palm in the middle of the front), snare -> Slap
// (fingers at the top edge), hi-hat -> a light tap. The drums are found when a song is prepared
// (cajon/server.ts) and kept with the song as one character per 16th note of its beat grid.
// From them come two parts to play: Groove (the section's repeating beat on 8th notes) and Easy
// (Bass and Slap on the beats). Songs without drums get a suggested groove instead.

export type Stroke = "B" | "S" | "t" | ".";
export type CajonLevel = "easy" | "groove";

/** 16th notes per beat */
export const SUBS = 4;
/** bars that share one groove (the drums' usual phrase) */
export const PHRASE_BARS = 4;

/** The song's drums, kept in the song doc (`doc.drums`). */
export interface DrumPart {
  v: 1;
  /** one character per 16th note from beat 0 (4 per beat): a digit whose bits say which drums
   *  play there, 1 kick, 2 snare, 4 hi-hat ("0" = none) */
  hits: string;
  /** how loud the drums Demucs pulled out are against the whole song (dB) */
  level?: number;
}

export interface DrumOnsets {
  kick: number[];
  snare: number[];
  hat: number[];
}

const BIT = { kick: 1, snare: 2, hat: 4 } as const;

/** Each drum's onsets (seconds) placed on the 16th notes of the beat grid; an onset counts at the
 *  nearest 16th when it is close enough (50 ms, or under half a 16th in fast songs). */
export function drumHits(on: DrumOnsets, beatTimes: readonly number[]): string {
  const grid: number[] = [];
  for (let i = 0; i + 1 < beatTimes.length; i++) {
    const step = (beatTimes[i + 1] - beatTimes[i]) / SUBS;
    for (let s = 0; s < SUBS; s++) grid.push(beatTimes[i] + s * step);
  }
  const bits = new Uint8Array(grid.length);
  for (const drum of ["kick", "snare", "hat"] as const) {
    for (const t of on[drum]) {
      let lo = 0;
      let hi = grid.length - 1;
      while (lo <= hi) {
        const m = (lo + hi) >> 1;
        if (grid[m] < t) lo = m + 1;
        else hi = m - 1;
      }
      if (!grid.length) break;
      const j = lo >= grid.length ? grid.length - 1 : lo > 0 && t - grid[lo - 1] < grid[lo] - t ? lo - 1 : lo;
      const step = j + 1 < grid.length ? grid[j + 1] - grid[j] : grid[j] - grid[j - 1];
      if (Math.abs(grid[j] - t) <= Math.min(0.05, 0.45 * step)) bits[j] |= BIT[drum];
    }
  }
  return Array.from(bits, (b) => String(b)).join("").replace(/0+$/, "");
}

/** The stroke a 16th note gets: the snare wins, then the kick, then the hi-hat. */
function strokeAt(hits: string, j: number): Stroke {
  const b = j < hits.length ? hits.charCodeAt(j) - 48 : 0;
  return b & 2 ? "S" : b & 1 ? "B" : b & 4 ? "t" : ".";
}

const isHit = (x: Stroke) => x === "B" || x === "S";

/** Per slot, the stroke at least half the bars play there (else a rest): steady, for Easy. */
function voteBar(played: Stroke[][]): Stroke[] {
  const out: Stroke[] = [];
  for (let s = 0; s < played[0].length; s++) {
    const count = { B: 0, S: 0, t: 0, ".": 0 };
    for (const bar of played) count[bar[s] ?? "."]++;
    const best = (["S", "B", "t"] as const).reduce((a, x) => (count[x] > count[a] ? x : a), "S" as "S" | "B" | "t");
    out.push(count[best] * 2 >= played.length ? best : ".");
  }
  return out;
}

/**
 * The bar a phrase plays. Groove: the bar its drummer plays most, when one comes back (a vote
 * per slot would join variants the drummer alternates into a bar nobody plays); else the vote.
 * Easy: the vote, which keeps a steady beat. Checked against MIDI drum tracks (Sweet Child O'
 * Mine: Groove bars matching 1% -> 18%; Easy as before). The bars are read at the level's detail.
 */
function phraseBar(strokes: Stroke[], slotsPerBar: number, from: number, bars: number, level: CajonLevel): Stroke[] {
  const raw: Stroke[][] = [];
  for (let b = from; b < from + bars; b++) raw.push(strokes.slice(b * slotsPerBar, (b + 1) * slotsPerBar));
  if (level === "easy") return onTheBeats(voteBar(raw));
  // bars the drums sit out don't count (else drums coming in mid-phrase would rest it)
  const played = raw.map(eighthNotes).filter((bar) => bar.some(isHit));
  const counts = new Map<string, number>();
  for (const bar of played) counts.set(bar.join(""), (counts.get(bar.join("")) ?? 0) + 1);
  const [top, n] = [...counts].reduce((a, x) => (x[1] > a[1] ? x : a), ["", 0] as [string, number]);
  return n >= 2 ? ([...top] as Stroke[]) : eighthNotes(voteBar(raw));
}

/** Groove level: the 8th notes (the "e" and "a" 16ths left out). */
export function eighthNotes(bar: readonly Stroke[]): Stroke[] {
  return bar.map((x, s) => (s % 2 ? "." : x));
}

/** Easy level: Bass and Slap on the beats only. When the drums sit half a beat off the grid
 *  (nothing on the beats, the strokes all on the "and"s), the "and"s are played instead. */
export function onTheBeats(bar: readonly Stroke[]): Stroke[] {
  const strokes = (phase: number) => bar.filter((x, s) => s % SUBS === phase && isHit(x)).length;
  const phase = strokes(0) === 0 && strokes(2) >= 2 ? 2 : 0;
  return bar.map((x, s) => (s % SUBS === phase && isHit(x) ? x : "."));
}

/** Which hand plays each stroke: 8th notes alternate right (on the beat) and left (the "and");
 *  on Easy the beats alternate, right on the odd ones. */
export function hands(bar: readonly Stroke[], level: CajonLevel): ("R" | "L" | "")[] {
  return bar.map((x, s) => {
    if (x === ".") return "";
    if (level === "easy") return Math.floor(s / SUBS) % 2 ? "L" : "R";
    return s % SUBS === 0 ? "R" : s % SUBS === 2 ? "L" : s % SUBS === 1 ? "L" : "R";
  });
}

export interface Groove {
  /** "A", "B", ... the same pattern keeps its letter wherever it comes back */
  name: string;
  /** one bar of strokes, SUBS per beat */
  bar: Stroke[];
  /** true for the stretches where the drums don't play */
  rest: boolean;
}

export interface CajonPart {
  level: CajonLevel;
  /** where it comes from: the song's drums, or a pattern that suits its tempo */
  source: "drums" | "suggested";
  beatsPerBar: number;
  /** the distinct grooves, in the order they first come */
  grooves: Groove[];
  /** the groove of each bar (index into `grooves`) */
  barGroove: number[];
  /** lots of strokes or a groove that keeps changing: Easy is the better start */
  busy: boolean;
}

/** Suggested grooves by tempo (one 4/4 bar, 16ths), for songs whose drums aren't known. */
const SUGGESTED: { upTo: number; groove: string }[] = [
  { upTo: 80, groove: "B...S.t.B.t.S..." },
  { upTo: 135, groove: "B.t.S.t.B.B.S.t." },
  { upTo: Infinity, groove: "B...S...B.B.S..." },
];

function suggestedBar(bpm: number, beatsPerBar: number): Stroke[] {
  if (beatsPerBar === 4) return [...SUGGESTED.find((s) => bpm <= s.upTo)!.groove] as Stroke[];
  // other meters: Bass on the one, Slap on the other beats, taps on the "and"s
  const bar: Stroke[] = [];
  for (let b = 0; b < beatsPerBar; b++) bar.push(b === 0 ? "B" : "S", ".", "t", ".");
  return bar;
}

/** Strokes in a part's bars (Bass and Slap) for judging how busy it is. */
const hitCount = (bar: readonly Stroke[]) => bar.filter(isHit).length;

export interface SongShape {
  beatsPerBar: number;
  bpm: number;
  /** song length in beats */
  length: number;
  drums?: DrumPart;
}

/** Real drums come out of a song at -13 to -1 dB against the whole of it; what Demucs still calls
 *  drums in a song without them is far quieter (-53 to -24 dB, mostly). */
export const DRUMS_LEVEL = -18;

/** For drums found before their level was kept: in real drumming the kick and snare seldom land
 *  together (6% of strokes is typical), while an acoustic song's strums set off both at once. */
const STRUM_SHARE = 0.45;

/** True when the drums found are too few, or too quiet, to play along with (an acoustic song). */
export function noDrums(drums: DrumPart | undefined, beats: number): boolean {
  if (!drums?.hits) return true;
  let strokes = 0;
  let both = 0;
  for (let i = 0; i < drums.hits.length; i++) {
    const b = (drums.hits.charCodeAt(i) - 48) & 3;
    if (b) strokes++;
    if (b === 3) both++;
  }
  // fewer than one kick or snare a bar on average
  if (strokes < Math.max(8, beats / 4)) return true;
  return drums.level !== undefined ? drums.level < DRUMS_LEVEL : both > STRUM_SHARE * strokes;
}

/** How far apart two grooves are: a stroke that differs counts 1, a tap or not 0.5. */
function distance(a: readonly Stroke[], b: readonly Stroke[]): number {
  let d = 0;
  for (let s = 0; s < a.length; s++) {
    if (a[s] === b[s]) continue;
    d += isHit(a[s]) || isHit(b[s]) ? 1 : 0.5;
  }
  return d;
}

/**
 * The cajon part of a song at a level: each 4-bar phrase plays the groove most of its bars
 * share; phrases whose grooves differ by a stroke or two (the drummer's variations, or a missed
 * hit) play the most common of them; stretches without drums rest. Without drums (or before
 * they are found), the whole song gets the suggested groove for its tempo.
 */
export function cajonPart(song: SongShape, level: CajonLevel): CajonPart {
  const bpb = Math.max(1, Math.round(song.beatsPerBar) || 4);
  const slots = bpb * SUBS;
  const bars = Math.max(1, Math.ceil(song.length / bpb));
  const shape = (bar: Stroke[]) => (level === "easy" ? onTheBeats(bar) : eighthNotes(bar));

  if (noDrums(song.drums, song.length)) {
    const bar = shape(suggestedBar(song.bpm, bpb));
    return { level, source: "suggested", beatsPerBar: bpb, grooves: [{ name: "A", bar, rest: false }], barGroove: new Array(bars).fill(0), busy: false };
  }

  const hits = song.drums!.hits;
  const strokes: Stroke[] = [];
  for (let j = 0; j < bars * slots; j++) strokes.push(strokeAt(hits, j));

  // each phrase's own groove (null: the drums are out)
  const phrases: { bars: number; bar: Stroke[] | null }[] = [];
  for (let b = 0; b < bars; b += PHRASE_BARS) {
    const n = Math.min(PHRASE_BARS, bars - b);
    const played = strokes.slice(b * slots, (b + n) * slots).filter(isHit).length;
    const bar = phraseBar(strokes, slots, b, n, level);
    // a phrase with less than a stroke or two a bar: rest
    phrases.push({ bars: n, bar: played < 2 * n || hitCount(bar) === 0 ? null : bar });
  }
  // grooves that differ by a stroke or two are one groove: the most common of them
  const counts = new Map<string, { bar: Stroke[]; bars: number }>();
  for (const p of phrases) {
    if (!p.bar) continue;
    const key = p.bar.join("");
    const c = counts.get(key) ?? { bar: p.bar, bars: 0 };
    c.bars += p.bars;
    counts.set(key, c);
  }
  const near = level === "easy" ? 1 : 2;
  const reps: Stroke[][] = [];
  const repOf = new Map<string, Stroke[]>();
  for (const [key, { bar }] of [...counts].sort((x, y) => y[1].bars - x[1].bars)) {
    const rep = reps.find((r) => distance(r, bar) <= near) ?? (reps.push(bar), bar);
    repOf.set(key, rep);
  }

  const grooves: Groove[] = [];
  const barGroove: number[] = [];
  let total = 0;
  let played = 0;
  for (const p of phrases) {
    const rep = p.bar ? repOf.get(p.bar.join(""))! : null;
    let index = grooves.findIndex((g) => (rep ? !g.rest && g.bar === rep : g.rest));
    if (index < 0) {
      index = grooves.length;
      grooves.push({ name: "", bar: rep ?? new Array<Stroke>(slots).fill("."), rest: !rep });
    }
    for (let i = 0; i < p.bars; i++) barGroove.push(index);
    if (rep) {
      total += hitCount(rep);
      played++;
    }
  }
  let letter = 0;
  for (const g of grooves) g.name = g.rest ? "Rest" : String.fromCharCode(65 + letter++);
  // busy: many strokes a bar, or no one groove that most of the song plays (it keeps changing)
  const perBar = played ? total / played : 0;
  const drummed = barGroove.filter((g) => !grooves[g].rest).length;
  const main = Math.max(0, ...grooves.map((g, i) => (g.rest ? 0 : barGroove.filter((x) => x === i).length)));
  const busy = level === "groove" && (perBar > bpb * 1.5 || (drummed >= 16 && main < 0.4 * drummed));
  return { level, source: "drums", beatsPerBar: bpb, grooves, barGroove, busy };
}

/** The level a song opens on: Groove, unless its groove is busy (then Easy). */
export function defaultLevel(song: SongShape): CajonLevel {
  return cajonPart(song, "groove").busy ? "easy" : "groove";
}

/** Every stroke of the part with its beat (from beat 0), for drawing and playing it. */
export function cajonStrokes(part: CajonPart): { beat: number; stroke: Exclude<Stroke, ".">; hand: "R" | "L" | "" }[] {
  const out: { beat: number; stroke: Exclude<Stroke, ".">; hand: "R" | "L" | "" }[] = [];
  const slots = part.beatsPerBar * SUBS;
  part.barGroove.forEach((g, b) => {
    const bar = part.grooves[g].bar;
    const hand = hands(bar, part.level);
    bar.forEach((x, s) => {
      if (x !== ".") out.push({ beat: (b * slots + s) / SUBS, stroke: x, hand: hand[s] });
    });
  });
  return out;
}

/** Checks a drum part from a song document (light: it is ours, but songs can be edited). */
export function validDrums(input: unknown): DrumPart | undefined {
  if (!input || typeof input !== "object") return undefined;
  const d = input as Record<string, unknown>;
  if (d.v !== 1 || typeof d.hits !== "string" || d.hits.length > 200_000 || !/^[0-7]*$/.test(d.hits)) return undefined;
  const level = typeof d.level === "number" && Number.isFinite(d.level) ? Math.max(-120, Math.min(20, d.level)) : undefined;
  return level === undefined ? { v: 1, hits: d.hits } : { v: 1, hits: d.hits, level };
}
