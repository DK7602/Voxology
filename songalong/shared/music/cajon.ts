// Cajon parts from a song's drums: kick -> Bass (palm in the middle of the front), snare -> Slap
// (fingers at the top edge), hi-hat -> a light tap. The drums are found when a song is prepared
// (cajon/server.ts) and kept with the song as one character per 16th note of its beat grid.
// From them come two parts to play: Groove (the section's repeating beat on 8th notes) and Easy
// (Bass and Slap on the beats). Songs without drums get a suggested groove instead.

/** Bass, Slap, tap, rest, or X: Bass and Slap at once with both hands (only in a player's fix) */
export type Stroke = "B" | "S" | "t" | "." | "X";
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

/** A backbeat needs this many snares heard without a kick on its beats, and this many times as
 *  many as on the other beats. */
const BACKBEAT_MIN = 8;
const BACKBEAT_RATIO = 4;

/**
 * A song's backbeat: the beats, every other one (0: from the first, 1: from the second), that
 * its snares heard without a kick fall on; null when they don't clearly (jazz, few snares, or an
 * odd number of beats to the bar).
 */
function backbeat(hits: string, beatsPerBar: number): 0 | 1 | null {
  if (beatsPerBar % 2) return null;
  const lone = [0, 0];
  for (let j = 0; j < hits.length; j += SUBS) if (((hits.charCodeAt(j) - 48) & 3) === 2) lone[(j / SUBS) % 2]++;
  for (const p of [0, 1] as const) if (lone[p] >= BACKBEAT_MIN && lone[p] >= BACKBEAT_RATIO * lone[1 - p]) return p;
  return null;
}

/**
 * The stroke a 16th note gets: the snare wins, then the kick, then the hi-hat. But a kick and
 * snare heard together on a beat off the backbeat play the kick: there the "snare" is mostly the
 * kick leaking into it (Thunderstruck read S on every beat, against its MIDI drum track 30% of
 * strokes right; this way 63%). On 23 hand-labelled songs (MDB-Drums) the snares this drops were
 * not played 35 times in 44, and none got worse.
 */
function strokeAt(hits: string, j: number, back: 0 | 1 | null): Stroke {
  const b = j < hits.length ? hits.charCodeAt(j) - 48 : 0;
  if ((b & 3) === 3 && back !== null && j % SUBS === 0 && (j / SUBS) % 2 !== back) return "B";
  return b & 2 ? "S" : b & 1 ? "B" : b & 4 ? "t" : ".";
}

const isHit = (x: Stroke) => x === "B" || x === "S" || x === "X";

/** Per slot, the stroke at least half the bars play there (else a rest): steady, for Easy. */
function voteBar(played: Stroke[][]): Stroke[] {
  const out: Stroke[] = [];
  for (let s = 0; s < played[0].length; s++) {
    const count = { B: 0, S: 0, t: 0, X: 0, ".": 0 };
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
  /** the bar as found, when the player has fixed it (see withFixes) */
  found?: Stroke[];
  /** a single bar the player changed: the index of the groove it changes (see withFixes) */
  barOf?: number;
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

/** The Bass and Slap strokes in a bar. */
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
    return { level, source: "suggested", beatsPerBar: bpb, grooves: [{ name: "A", bar, rest: false }], barGroove: new Array(bars).fill(0) };
  }

  const hits = song.drums!.hits;
  const strokes: Stroke[] = [];
  const back = backbeat(hits, bpb);
  for (let j = 0; j < bars * slots; j++) strokes.push(strokeAt(hits, j, back));

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
  for (const p of phrases) {
    const rep = p.bar ? repOf.get(p.bar.join(""))! : null;
    let index = grooves.findIndex((g) => (rep ? !g.rest && g.bar === rep : g.rest));
    if (index < 0) {
      index = grooves.length;
      grooves.push({ name: "", bar: rep ?? new Array<Stroke>(slots).fill("."), rest: !rep });
    }
    for (let i = 0; i < p.bars; i++) barGroove.push(index);
  }
  let letter = 0;
  for (const g of grooves) g.name = g.rest ? "Rest" : String.fromCharCode(65 + letter++);
  return { level, source: "drums", beatsPerBar: bpb, grooves, barGroove };
}

/** A song opens on Easy unless the player picked Groove: checked against MIDI drum tracks, Easy
 *  matched 62-99% of strokes, Groove 63-82%. */
export const DEFAULT_LEVEL: CajonLevel = "easy";

/** A player's fixes to a part at one level: each groove as found (its strokes as a string) ->
 *  the groove to play instead, wherever it comes in the song. */
export type CajonFixes = Record<string, string>;

/** A player's changes to single bars at one level, by bar (from 0): what the bar's groove was as
 *  found (so a change can't land on another bar if the song's beats are ever found again) and the
 *  bar to play. */
export type CajonBarFixes = Record<string, { found: string; bar: string }>;

/**
 * The part with the player's fixes in place: their grooves wherever those come, then their single
 * bars, each a groove of its own that knows the groove it changes (`barOf`). A fix whose groove
 * is no longer found there does nothing.
 */
export function withFixes(part: CajonPart, fixes: CajonFixes | undefined, barFixes?: CajonBarFixes): CajonPart {
  const hasFixes = !!fixes && Object.keys(fixes).length > 0;
  const hasBars = !!barFixes && Object.keys(barFixes).length > 0;
  if (!hasFixes && !hasBars) return part;
  const grooves = part.grooves.map((g) => {
    const fix = g.rest || !fixes ? undefined : fixes[g.bar.join("")];
    return fix && fix.length === g.bar.length ? { ...g, bar: [...fix] as Stroke[], found: g.bar } : g;
  });
  const barGroove = [...part.barGroove];
  for (const [key, { found, bar }] of Object.entries(barFixes ?? {})) {
    const b = Number(key);
    const of = barGroove[b];
    const g = grooves[of];
    if (!g || g.barOf !== undefined || (g.found ?? g.bar).join("") !== found || bar.length !== g.bar.length) continue;
    barGroove[b] = grooves.push({ name: g.name, bar: [...bar] as Stroke[], rest: false, barOf: of }) - 1;
  }
  return { ...part, grooves, barGroove };
}

/** The bar fixes after the player changes bar `bar` (its groove `base`, as found `found`) to
 *  `next`; back to its groove, the change goes. */
export function setBarFix(
  fixes: CajonBarFixes | undefined,
  bar: number,
  found: readonly Stroke[],
  base: readonly Stroke[],
  next: readonly Stroke[],
): CajonBarFixes {
  const out = { ...fixes };
  if (next.join("") === base.join("")) delete out[bar];
  else out[bar] = { found: found.join(""), bar: next.join("") };
  return out;
}

/** The fixes after the player changes a groove (`found` as found) to `bar`; back to how it was
 *  found, the fix goes. */
export function setFix(fixes: CajonFixes | undefined, found: readonly Stroke[], bar: readonly Stroke[]): CajonFixes {
  const out = { ...fixes };
  const key = found.join("");
  if (bar.join("") === key) delete out[key];
  else out[key] = bar.join("");
  return out;
}

/** The most fixes kept per level (grooves, and single bars), and the longest bar (12 beats of 16ths). */
export const MAX_FIXES = 64;
/** (150 a level keeps saved settings well under the 64 KB a request may be, even in 12/8) */
export const MAX_BAR_FIXES = 150;
const MAX_BAR = 48;
const BAR = new RegExp(`^[BStX.]{1,${MAX_BAR}}$`);

/** Checks a player's fixes from their saved settings: strokes only, a bar for a bar. */
export function validFixes(input: unknown): Partial<Record<CajonLevel, CajonFixes>> | undefined {
  if (!input || typeof input !== "object") return undefined;
  const out: Partial<Record<CajonLevel, CajonFixes>> = {};
  for (const level of ["easy", "groove"] as const) {
    const fixes = (input as Record<string, unknown>)[level];
    if (!fixes || typeof fixes !== "object") continue;
    const kept: CajonFixes = {};
    for (const [k, v] of Object.entries(fixes).slice(0, MAX_FIXES)) {
      if (typeof v === "string" && BAR.test(k) && BAR.test(v) && k.length === v.length) kept[k] = v;
    }
    if (Object.keys(kept).length) out[level] = kept;
  }
  return Object.keys(out).length ? out : undefined;
}

/** Checks a player's single-bar changes from their saved settings. */
export function validBarFixes(input: unknown): Partial<Record<CajonLevel, CajonBarFixes>> | undefined {
  if (!input || typeof input !== "object") return undefined;
  const out: Partial<Record<CajonLevel, CajonBarFixes>> = {};
  for (const level of ["easy", "groove"] as const) {
    const fixes = (input as Record<string, unknown>)[level];
    if (!fixes || typeof fixes !== "object") continue;
    const kept: CajonBarFixes = {};
    for (const [k, v] of Object.entries(fixes).slice(0, MAX_BAR_FIXES)) {
      if (!/^\d{1,5}$/.test(k) || !v || typeof v !== "object") continue;
      const { found, bar } = v as Record<string, unknown>;
      if (typeof found === "string" && typeof bar === "string" && BAR.test(found) && BAR.test(bar) && found.length === bar.length) {
        kept[k] = { found, bar };
      }
    }
    if (Object.keys(kept).length) out[level] = kept;
  }
  return Object.keys(out).length ? out : undefined;
}

/** Every stroke of the part with its beat (from beat 0), for drawing and playing it. */
/** The two hands of a double hit: the hand whose turn it is slaps, the other plays the bass. */
export function doubleHit(hand: "R" | "L" | ""): { S: "R" | "L"; B: "R" | "L" } {
  return hand === "L" ? { S: "L", B: "R" } : { S: "R", B: "L" };
}

/** Every stroke of the part with its beat (from beat 0), for drawing and playing it; a double
 *  hit is its Bass and its Slap, each with its hand. */
export function cajonStrokes(part: CajonPart): { beat: number; stroke: "B" | "S" | "t"; hand: "R" | "L" | "" }[] {
  const out: { beat: number; stroke: "B" | "S" | "t"; hand: "R" | "L" | "" }[] = [];
  const slots = part.beatsPerBar * SUBS;
  part.barGroove.forEach((g, b) => {
    const bar = part.grooves[g].bar;
    const hand = hands(bar, part.level);
    bar.forEach((x, s) => {
      const beat = (b * slots + s) / SUBS;
      if (x === "X") {
        const both = doubleHit(hand[s]);
        out.push({ beat, stroke: "B", hand: both.B }, { beat, stroke: "S", hand: both.S });
      } else if (x !== ".") out.push({ beat, stroke: x, hand: hand[s] });
    });
  });
  return out;
}

/** A step of the Fix grid tapped in a row: the row's stroke goes there, or goes if it is there;
 *  Bass and Slap together make a double hit. */
export function tapStroke(current: Stroke, row: "B" | "S" | "t"): Stroke {
  if (row === "t") return current === "t" ? "." : "t";
  const other = row === "B" ? "S" : "B";
  if (current === row) return ".";
  if (current === "X") return other;
  if (current === other) return "X";
  return row;
}

/** Checks a drum part from a song document (light: it is ours, but songs can be edited). */
export function validDrums(input: unknown): DrumPart | undefined {
  if (!input || typeof input !== "object") return undefined;
  const d = input as Record<string, unknown>;
  if (d.v !== 1 || typeof d.hits !== "string" || d.hits.length > 200_000 || !/^[0-7]*$/.test(d.hits)) return undefined;
  const level = typeof d.level === "number" && Number.isFinite(d.level) ? Math.max(-120, Math.min(20, d.level)) : undefined;
  return level === undefined ? { v: 1, hits: d.hits } : { v: 1, hits: d.hits, level };
}
