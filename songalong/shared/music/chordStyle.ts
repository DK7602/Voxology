// Chord styles: a song's chords as recorded, as full chords, or as power chords, for playing a rock
// song on an acoustic guitar or an acoustic song on a distorted electric. Power chords are each
// chord's root and fifth. Full chords fill in what a power chord leaves out, its third (major or
// minor): from the melody heard over it (the song's lead part) when that says, else from the key.

import type { ChordEvent } from "../types";
import { parseChord } from "./chords";
import { mod12, type Key } from "./notes";

export type ChordStyle = "recorded" | "full" | "power";
export const CHORD_STYLES: readonly ChordStyle[] = ["recorded", "full", "power"];

/** How a power chord's third was found. */
export type FilledBy = "melody" | "key";

/** The melody decides when it spends this many beats on one of the thirds, and twice as long as
 *  on the other. */
const MELODY_MIN_BEATS = 1;
const MELODY_RATIO = 2;

/** Major ("") or minor ("m") by scale degree (semitones above the tonic). A minor key's V is
 *  major, as it mostly is in pop and rock; a diminished degree is taken as minor (a power chord's
 *  perfect fifth rules diminished out). Roots outside the key are major (rock's borrowed bVII, bVI,
 *  bIII). */
const IN_MAJOR: Record<number, "" | "m"> = { 0: "", 2: "m", 4: "m", 5: "", 7: "", 9: "m", 11: "m" };
const IN_MINOR: Record<number, "" | "m"> = { 0: "m", 2: "m", 3: "", 5: "m", 7: "", 8: "", 10: "" };

export interface FullChords {
  /** each power chord of the song (as written) -> the full chord it plays as */
  chords: Map<string, string>;
  /** each power chord -> how its third was found */
  by: Map<string, FilledBy>;
}

/** Each event's span in beats (to the next event, the last to the song's end). */
function spans(events: readonly ChordEvent[], length: number): { e: ChordEvent; from: number; to: number }[] {
  const sorted = [...events].sort((a, b) => a.beat - b.beat);
  return sorted.map((e, i) => ({ e, from: e.beat, to: i + 1 < sorted.length ? sorted[i + 1].beat : Math.max(length, e.beat) }));
}

/**
 * Full chords for a song's power chords. Each power chord (all its times in the song together)
 * gets the third the melody sings or plays over it, when the melody clearly favours one; else
 * the key's.
 */
export function fillPowerChords(chords: readonly ChordEvent[], lead: readonly ChordEvent[] | undefined, key: Key | null, length: number): FullChords {
  const melody = spans(lead ?? [], length).flatMap(({ e, from, to }) => {
    const c = parseChord(e.name);
    if (!c || (c.category !== "note" && c.category !== "notes")) return [];
    const pcs = c.category === "note" ? [c.root] : c.sequence ?? [];
    return pcs.map((pc) => ({ pc, from, to, share: 1 / pcs.length }));
  });
  // melody beats on each pitch class, by power chord
  const heard = new Map<string, number[]>();
  for (const { e, from, to } of spans(chords, length)) {
    const c = parseChord(e.name);
    if (c?.category !== "power") continue;
    const w = heard.get(e.name) ?? new Array<number>(12).fill(0);
    for (const m of melody) {
      const overlap = Math.min(to, m.to) - Math.max(from, m.from);
      if (overlap > 0) w[m.pc] += overlap * m.share;
    }
    heard.set(e.name, w);
  }
  const out: FullChords = { chords: new Map(), by: new Map() };
  for (const [symbol, w] of heard) {
    const c = parseChord(symbol)!;
    const minor = w[mod12(c.root + 3)];
    const major = w[mod12(c.root + 4)];
    let quality: "" | "m";
    if (Math.max(minor, major) >= MELODY_MIN_BEATS && Math.max(minor, major) >= MELODY_RATIO * Math.min(minor, major)) {
      quality = major > minor ? "" : "m";
      out.by.set(symbol, "melody");
    } else {
      quality = key ? ((key.minor ? IN_MINOR : IN_MAJOR)[mod12(c.root - key.tonic)] ?? "") : "";
      out.by.set(symbol, "key");
    }
    out.chords.set(symbol, c.rootName + quality + (c.bass !== undefined && c.bassName ? "/" + c.bassName : ""));
  }
  return out;
}

/** A chord as a power chord: its root and fifth. Single notes, and chords without a perfect fifth
 *  (diminished, augmented), stay as they are; a slash chord's bass goes (a power chord's bass is
 *  its root). */
export function powerChordOf(symbol: string): string {
  const c = parseChord(symbol);
  if (!c || c.category === "none" || c.category === "note" || c.category === "notes" || c.category === "power") return symbol;
  return c.intervals.includes(7) ? c.rootName + "5" : symbol;
}
