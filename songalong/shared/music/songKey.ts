// Which key a song is in, for naming its chords and notes: the key the song gives, unless its
// chords clearly belong to another (a chord sheet pasted without a key line used to count as C).

import { displayChord, parseChord, transposeSymbol, type Chord } from "./chords";
import { mod12, parseKey, prettyAccidentals, usualKey, type Key } from "./notes";
import { chordEnd } from "../timeline";
import type { ChordEvent } from "../types";

export interface WeightedChord {
  name: string;
  /** how much it counts: beats played, or 1 each */
  weight: number;
}

type Kind = "maj" | "min" | "dim" | "any";

/** The chords at home in each kind of key: [semitones above the tonic, kind, how well it fits].
 *  Major keys include the flat seventh (D in E), minor keys the major fifth (E in A minor). */
const MAJOR: [number, Kind, number][] = [
  [0, "maj", 1], [2, "min", 1], [4, "min", 1], [5, "maj", 1], [7, "maj", 1], [9, "min", 1], [11, "dim", 1], [10, "maj", 0.7],
];
const MINOR: [number, Kind, number][] = [
  [0, "min", 1], [2, "dim", 1], [3, "maj", 1], [5, "min", 1], [7, "min", 1], [7, "maj", 1], [8, "maj", 1], [10, "maj", 1], [11, "dim", 1],
];

function kindOf(c: Chord): Kind {
  if (c.category === "power" || c.suffix.startsWith("sus")) return "any";
  const has = (i: number) => c.intervals.includes(i);
  if (has(3) && has(6) && !has(7)) return "dim";
  if (has(3) && !has(4)) return "min";
  if (has(4)) return "maj";
  return "any";
}

/** How well a chord belongs in `key`: 1 at home, 0.4 on a scale note with another quality, 0 off it. */
function fit(root: number, kind: Kind, key: Key): number {
  const deg = mod12(root - key.tonic);
  const homes = (key.minor ? MINOR : MAJOR).filter(([d]) => d === deg);
  if (!homes.length) return 0;
  const match = homes.filter(([, k]) => kind === "any" || k === kind).map(([, , w]) => w);
  return match.length ? Math.max(...match) : 0.4;
}

interface Parsed {
  root: number;
  kind: Kind;
  weight: number;
}

function parsedChords(chords: readonly WeightedChord[]): Parsed[] {
  const out: Parsed[] = [];
  for (const { name, weight } of chords) {
    const c = parseChord(name);
    if (!c || c.category === "none" || c.category === "note" || c.category === "notes") continue;
    out.push({ root: c.root, kind: kindOf(c), weight });
  }
  return out;
}

/** Share of the playing time that is at home in `key` (0..1). */
function share(chords: Parsed[], key: Key): number {
  const total = chords.reduce((a, c) => a + c.weight, 0);
  return total ? chords.reduce((a, c) => a + c.weight * fit(c.root, c.kind, key), 0) / total : 0;
}

/** How well the chords suit `key`: their share at home in it, plus a little for its home chord
 *  being played a lot and starting or ending the song (which tells C from A minor). */
function score(chords: Parsed[], key: Key): number {
  const total = chords.reduce((a, c) => a + c.weight, 0);
  const isHome = (c: Parsed | undefined) => !!c && c.root === key.tonic && c.kind === (key.minor ? "min" : "maj");
  let s = share(chords, key) + (0.3 * chords.filter((c) => isHome(c)).reduce((a, c) => a + c.weight, 0)) / total;
  if (isHome(chords[0])) s += 0.05;
  if (isHome(chords.at(-1))) s += 0.08;
  return s;
}

/** The key the chords suggest, or null without chords. */
export function keyFromChords(chords: readonly WeightedChord[]): Key | null {
  const parsed = parsedChords(chords);
  if (!parsed.length) return null;
  let best: Key | null = null;
  let bestScore = -1;
  for (const minor of [false, true]) {
    for (let tonic = 0; tonic < 12; tonic++) {
      const key = usualKey(tonic, minor);
      const s = score(parsed, key);
      if (s > bestScore + 1e-9) {
        best = key;
        bestScore = s;
      }
    }
  }
  return best;
}

/** The key to name a song's notes in: the given key (by its usual name) unless the chords clearly
 *  suit another, else the key the chords suggest. Null when there is neither. */
export function songKey(keyName: string, chords: readonly WeightedChord[]): Key | null {
  const given = parseKey(keyName);
  const said = given && usualKey(given.tonic, given.minor);
  const heard = keyFromChords(chords);
  if (!said || !heard) return said ?? heard;
  const parsed = parsedChords(chords);
  return score(parsed, said) >= score(parsed, heard) - 0.1 ? said : heard;
}

/** Each chord of a song with the beats it is played for. */
export function weightedChords(chords: readonly ChordEvent[], length: number): WeightedChord[] {
  return chords.map((c, i) => ({ name: c.name, weight: Math.max(0.5, chordEnd(chords, i, length) - c.beat) }));
}

/** A list of chord names, each counting once (song lists, which have no timing). */
export function listedChords(names: readonly string[]): WeightedChord[] {
  return names.map((name) => ({ name, weight: 1 }));
}

/** A listed song's key and chords, named the way its song page names them (shown with ♯ / ♭). */
export function namedForList(keyName: string, chords: readonly string[]): { key: string; chords: string[] } {
  const key = songKey(keyName, listedChords(chords));
  const named = key ? chords.map((c) => transposeSymbol(c, 0, key)) : [...chords];
  return { key: prettyAccidentals(key?.name ?? keyName), chords: named.map((c) => displayChord(c).text) };
}
