// How to play a chord: fingerings for guitar, ukulele and mandolin, keys for piano.
//
// Fretted voicings come from a small table of the standard open shapes (so beginners see the
// shapes they know first) plus a search over the fretboard that scores every playable shape:
// all chord tones present, root (or slash bass) lowest, at most four fingers (a barre counts
// as one), small stretch, low position, open strings welcome.

import { type Chord, chordPitchClasses, noteMidi, parseChord } from "./chords";
import { mod12 } from "./notes";
import type { Instrument } from "../types";

export interface FrettedInstrument {
  id: Exclude<Instrument, "piano">;
  name: string;
  /** open-string MIDI notes, in diagram order (lowest string first) */
  tuning: number[];
  tuningNames: string[];
  /** frets a hand can cover: frets f .. f + span */
  span: number;
  /** fewest strings a full chord should use */
  minStrings: number;
  /** how much a root-in-the-bass matters (guitar: a lot; re-entrant ukulele: not at all) */
  bassWeight: number;
}

export const GUITAR: FrettedInstrument = {
  id: "guitar", name: "Guitar", tuning: [40, 45, 50, 55, 59, 64], tuningNames: ["E", "A", "D", "G", "B", "E"],
  span: 3, minStrings: 4, bassWeight: 1,
};
export const UKULELE: FrettedInstrument = {
  id: "ukulele", name: "Ukulele", tuning: [67, 60, 64, 69], tuningNames: ["G", "C", "E", "A"],
  span: 3, minStrings: 4, bassWeight: 0,
};
export const MANDOLIN: FrettedInstrument = {
  id: "mandolin", name: "Mandolin", tuning: [55, 62, 69, 76], tuningNames: ["G", "D", "A", "E"],
  span: 4, minStrings: 3, bassWeight: 0.4,
};
export const FRETTED: Record<FrettedInstrument["id"], FrettedInstrument> = {
  guitar: GUITAR, ukulele: UKULELE, mandolin: MANDOLIN,
};

export interface Barre {
  fret: number;
  from: number; // string index (inclusive)
  to: number;
  finger: number;
}

export interface FretVoicing {
  /** per string: null = not played (x), 0 = open, n = fret (relative to the capo) */
  frets: (number | null)[];
  /** per string: 1 index .. 4 little finger; null for open / muted */
  fingers: (number | null)[];
  barres: Barre[];
  /** first fret drawn in the diagram (1 = the nut is shown) */
  baseFret: number;
  /** lower is easier */
  difficulty: number;
  /** order labels for single notes / riffs (1, 2, 3 ...) instead of fingers */
  order?: (number | null)[];
}

const MAX_FRET = 15;

// Standard open / first-position shapes, lowest string first. "x" = muted.
const GUITAR_SHAPES: Record<string, string> = {
  C: "x32010", C7: "x32310", Cmaj7: "x32000", Cadd9: "x32030", Csus2: "x30033", Csus4: "x33011", "C/G": "332010", "C/E": "032010", C6: "x32210",
  D: "xx0232", Dm: "xx0231", D7: "xx0212", Dm7: "xx0211", Dmaj7: "xx0222", Dsus2: "xx0230", Dsus4: "xx0233", "D/F#": "200232", D6: "xx0202", Dadd9: "xx0230",
  E: "022100", Em: "022000", E7: "020100", Em7: "022030", Emaj7: "021100", Esus4: "022200", E5: "022xxx", Eadd9: "024100", Em9: "024000",
  F: "133211", Fmaj7: "xx3210", "F/C": "x33211", Fm: "133111", F5: "133xxx", Fsus2: "xx3011",
  G: "320003", G7: "320001", Gmaj7: "320002", "G/B": "x20003", Gsus4: "330013", G5: "355xxx", G6: "320000", Gadd9: "320203",
  A: "x02220", Am: "x02210", A7: "x02020", Am7: "x02010", Amaj7: "x02120", Asus2: "x02200", Asus4: "x02230", A5: "x022xx", A6: "x02222", "Am/G": "302210",
  B7: "x21202", Bm: "x24432", B: "x24442", Bm7: "x20202", B5: "x244xx",
};
const UKULELE_SHAPES: Record<string, string> = {
  C: "0003", Cm: "0333", C7: "0001", Cmaj7: "0002",
  D: "2220", Dm: "2210", D7: "2223", Dm7: "2213",
  E: "1402", Em: "0432", E7: "1202", Em7: "0202",
  F: "2010", Fm: "1013", Fmaj7: "2413",
  G: "0232", Gm: "0231", G7: "0212", Gmaj7: "0222",
  A: "2100", Am: "2000", A7: "0100", Am7: "0000",
  Bb: "3211", B: "4322", Bm: "4222", B7: "2322",
};
const MANDOLIN_SHAPES: Record<string, string> = {
  G: "0023", C: "0230", D: "2002", Em: "0220", Am: "2235", A: "2245", E: "1224", Dm: "2001",
};
const TABLES: Record<FrettedInstrument["id"], Record<string, string>> = {
  guitar: GUITAR_SHAPES, ukulele: UKULELE_SHAPES, mandolin: MANDOLIN_SHAPES,
};

function parseShape(s: string): (number | null)[] {
  return [...s].map((ch) => (ch === "x" ? null : parseInt(ch, 36)));
}

/** Assigns fingers (and barres) to a shape. Returns null when it needs more than four fingers. */
export function assignFingers(frets: (number | null)[]): Pick<FretVoicing, "fingers" | "barres"> | null {
  const fingers: (number | null)[] = frets.map(() => null);
  const barres: Barre[] = [];
  const fretted = frets.map((f, i) => ({ f, i })).filter((x) => x.f !== null && x.f > 0) as { f: number; i: number }[];
  if (fretted.length === 0) return { fingers, barres };

  let notes = fretted;
  let next = 1;
  if (fretted.length > 4) {
    // Index-finger barre across the lowest fret, from its lowest string up to its highest,
    // as long as nothing in between is open or muted.
    const minF = Math.min(...fretted.map((x) => x.f));
    const at = fretted.filter((x) => x.f === minF).map((x) => x.i);
    const from = at[0];
    const to = at[at.length - 1];
    let ok = at.length >= 2;
    for (let s = from; s <= to && ok; s++) if (frets[s] === null || frets[s]! < minF) ok = false;
    if (ok) {
      barres.push({ fret: minF, from, to, finger: 1 });
      for (const s of at) fingers[s] = 1;
      notes = fretted.filter((x) => x.f !== minF);
      next = 2;
    }
  }
  // Remaining notes: lower fret first, then lower string. Several notes at one fret on
  // adjacent strings can share a finger (a small barre) when fingers run short.
  notes = [...notes].sort((a, b) => a.f - b.f || a.i - b.i);
  const groups: { f: number; strings: number[] }[] = [];
  for (const x of notes) groups.push({ f: x.f, strings: [x.i] });
  while (groups.length + next - 1 > 4) {
    // merge the first same-fret pair of adjacent strings (nothing lower in between)
    let merged = false;
    for (let g = groups.length - 1; g > 0 && !merged; g--) {
      const a = groups[g - 1];
      const b = groups[g];
      if (a.f !== b.f) continue;
      const lo = Math.min(...a.strings, ...b.strings);
      const hi = Math.max(...a.strings, ...b.strings);
      let ok = true;
      for (let s = lo; s <= hi; s++) if (frets[s] === null || frets[s]! < a.f) ok = false;
      if (ok) {
        a.strings.push(...b.strings);
        groups.splice(g, 1);
        merged = true;
      }
    }
    if (!merged) return null;
  }
  for (const g of groups) {
    const finger = next++;
    for (const s of g.strings) fingers[s] = finger;
    if (g.strings.length > 1) {
      barres.push({ fret: g.f, from: Math.min(...g.strings), to: Math.max(...g.strings), finger });
    }
  }
  return { fingers, barres };
}

function baseFretOf(frets: (number | null)[], span: number): number {
  const fretted = frets.filter((f): f is number => f !== null && f > 0);
  if (!fretted.length) return 1;
  const max = Math.max(...fretted);
  if (max <= span + 1) return 1;
  return Math.min(...fretted);
}

interface Target {
  pcs: Set<number>;
  required: number[];
  optional: Set<number>;
  bass: number;
  power: boolean;
}

function targetFor(chord: Chord): Target {
  const pcs = new Set(chordPitchClasses(chord));
  const optional = new Set(chord.optional.map(mod12));
  const required = [...new Set(chord.intervals.map((i) => mod12(chord.root + i)))].filter(
    (pc) => !optional.has(mod12(pc - chord.root)),
  );
  if (chord.bass !== undefined && !required.includes(chord.bass)) required.push(chord.bass);
  return {
    pcs,
    required,
    optional: new Set([...optional].map((o) => mod12(chord.root + o))),
    bass: chord.bass ?? chord.root,
    power: chord.category === "power",
  };
}

/** Scores a shape; null when it isn't a usable voicing of the target. */
function evaluate(frets: (number | null)[], inst: FrettedInstrument, t: Target): FretVoicing | null {
  const played = frets.map((f, i) => (f === null ? null : inst.tuning[i] + f));
  const sounding = played.filter((m): m is number => m !== null);
  if (sounding.length < (t.power ? 2 : Math.min(inst.minStrings - 1, t.pcs.size))) return null;
  const present = new Set(sounding.map(mod12));
  for (const pc of t.required) if (!present.has(pc)) return null;
  for (const pc of present) if (!t.pcs.has(pc)) return null;

  const lowest = Math.min(...sounding);
  const bassOk = mod12(lowest) === t.bass;
  if (!bassOk && (inst.bassWeight >= 1 || t.power)) return null;

  const fingering = assignFingers(frets);
  if (!fingering) return null;

  const fretted = frets.filter((f): f is number => f !== null && f > 0);
  const minF = fretted.length ? Math.min(...fretted) : 0;
  const maxF = fretted.length ? Math.max(...fretted) : 0;
  const firstPlayed = frets.findIndex((f) => f !== null);
  const lastPlayed = frets.length - 1 - [...frets].reverse().findIndex((f) => f !== null);
  let interiorMutes = 0;
  for (let s = firstPlayed; s <= lastPlayed; s++) if (frets[s] === null) interiorMutes++;
  if (interiorMutes > (inst.tuning.length >= 6 ? 1 : 0)) return null;
  const bassMutes = firstPlayed;
  const topMutes = frets.length - 1 - lastPlayed;
  const opens = frets.filter((f) => f === 0).length;
  const fingersUsed = new Set(fingering.fingers.filter((f) => f !== null)).size;
  const hasBigBarre = fingering.barres.some((b) => b.to - b.from >= 2);

  let d = 0;
  d += minF * 0.15;
  d += fingersUsed * 0.4;
  d += hasBigBarre ? 0.8 : 0;
  d += (maxF - minF) * 0.5;
  d += bassMutes * 0.6 + topMutes * 1.2 + interiorMutes * 2;
  if (minF <= 4) d -= opens * 0.25;
  if (!bassOk) d += 3 * inst.bassWeight;
  const missingOptional = [...t.optional].filter((pc) => !present.has(pc)).length;
  d += missingOptional * 0.6;
  if (t.power) {
    // root + fifth (+ octave) on adjacent low strings; the muted top strings are the point
    d += Math.abs(sounding.length - 3) * 0.5 + firstPlayed * 0.8 - topMutes * 1.2;
  } else if (sounding.length < inst.tuning.length) {
    d += (inst.tuning.length - sounding.length) * 0.6;
  }
  return { frets, ...fingering, baseFret: baseFretOf(frets, inst.span), difficulty: Math.round(d * 100) / 100 };
}

function search(inst: FrettedInstrument, t: Target): FretVoicing[] {
  const found = new Map<string, FretVoicing>();
  const n = inst.tuning.length;
  for (let w = 0; w <= MAX_FRET - inst.span; w++) {
    const options: (number | null)[][] = inst.tuning.map((open) => {
      const o: (number | null)[] = [null];
      if (t.pcs.has(mod12(open))) o.push(0);
      for (let f = Math.max(1, w); f <= w + inst.span; f++) if (t.pcs.has(mod12(open + f))) o.push(f);
      return o;
    });
    const cur: (number | null)[] = new Array(n).fill(null);
    const rec = (s: number) => {
      if (s === n) {
        const key = cur.join(",");
        if (found.has(key)) return;
        const v = evaluate([...cur], inst, t);
        if (v) found.set(key, v);
        return;
      }
      for (const f of options[s]) {
        cur[s] = f;
        rec(s + 1);
      }
      cur[s] = null;
    };
    rec(0);
  }
  return [...found.values()];
}

// Movable barre shapes: frets relative to the root fret, root on the low E string ("E shape")
// or on the A string ("A shape"). These are what players learn for every chord without an
// open shape, so they are offered first.
const E_SHAPES: Record<string, string> = {
  "": "022100", m: "022000", "7": "020100", m7: "020000", maj7: "021100", sus4: "022200", "5": "022xxx",
  "6": "022120", m6: "022020", "9": "020102",
};
const A_SHAPES: Record<string, string> = {
  "": "x02220", m: "x02210", "7": "x02020", m7: "x02010", maj7: "x02120", sus4: "x02230", sus2: "x02200",
  "5": "x022xx", "6": "x02222", m7b5: "x0101x", "7sus4": "x02030",
};

function movableShape(chord: Chord): (number | null)[] | null {
  if (chord.bass !== undefined) return null;
  const options: (number | null)[][] = [];
  const eShape = E_SHAPES[chord.suffix];
  const aShape = A_SHAPES[chord.suffix];
  const eFret = mod12(chord.root - 4);
  const aFret = mod12(chord.root - 9);
  if (eShape && eFret > 0) options.push(parseShape(eShape).map((f) => (f === null ? null : f + eFret)));
  if (aShape && aFret > 0) options.push(parseShape(aShape).map((f) => (f === null ? null : f + aFret)));
  if (!options.length) return null;
  const pos = (fr: (number | null)[]) => Math.min(...fr.filter((f): f is number => f !== null));
  options.sort((a, b) => pos(a) - pos(b));
  return options[0];
}

const voicingCache = new Map<string, FretVoicing[]>();

/** Ways to play `symbol` on a fretted instrument, easiest first (at most `max`). */
export function fretVoicings(symbol: string, instId: FrettedInstrument["id"], max = 8): FretVoicing[] {
  const cacheKey = instId + "|" + symbol;
  const hit = voicingCache.get(cacheKey);
  if (hit) return hit.slice(0, max);
  const inst = FRETTED[instId];
  const chord = parseChord(symbol);
  let result: FretVoicing[] = [];
  if (chord && (chord.category === "note" || chord.category === "notes")) {
    result = noteVoicings(chord, inst);
  } else if (chord && chord.category !== "none") {
    const t = targetFor(chord);
    const all = search(inst, t).sort((a, b) => a.difficulty - b.difficulty);
    const table = TABLES[instId];
    const canonical = Object.keys(table).find((k) => sameChord(k, chord));
    const standard = canonical ? parseShape(table[canonical]) : instId === "guitar" ? movableShape(chord) : null;
    if (standard) {
      const v = evaluate(standard, inst, t) ?? {
        frets: standard, ...(assignFingers(standard) ?? { fingers: standard.map(() => null), barres: [] }),
        baseFret: baseFretOf(standard, inst.span), difficulty: 0,
      };
      v.difficulty = Math.min(v.difficulty, all[0]?.difficulty ?? v.difficulty) - 0.01;
      result = [v, ...all.filter((x) => x.frets.join() !== standard.join())];
    } else {
      result = all;
    }
    result = diverse(result);
  }
  voicingCache.set(cacheKey, result);
  return result.slice(0, max);
}

function sameChord(tableName: string, chord: Chord): boolean {
  const c = parseChord(tableName);
  return !!c && c.root === chord.root && c.suffix === chord.suffix && c.bass === chord.bass;
}

/** Keeps the best shape per area of the neck so the alternatives are genuinely different. */
function diverse(sorted: FretVoicing[]): FretVoicing[] {
  const out: FretVoicing[] = [];
  const seenPos = new Map<number, number>();
  for (const v of sorted) {
    const fretted = v.frets.filter((f): f is number => f !== null && f > 0);
    const pos = fretted.length ? Math.min(...fretted) : 0;
    const count = seenPos.get(pos) ?? 0;
    if (count >= 2) continue;
    seenPos.set(pos, count + 1);
    out.push(v);
    if (out.length >= 12) break;
  }
  return out;
}

/** Single notes / riffs: the lowest comfortable position for each note, keeping the hand still. */
function noteVoicings(chord: Chord, inst: FrettedInstrument): FretVoicing[] {
  const seq = chord.category === "notes" ? chord.sequence ?? [chord.root] : [chord.root];
  const n = inst.tuning.length;
  const results: FretVoicing[] = [];
  // Try each hand position (window start) and place every note in it.
  for (let w = 0; w <= 9; w++) {
    const frets: (number | null)[] = new Array(n).fill(null);
    const order: (number | null)[] = new Array(n).fill(null);
    const fingers: (number | null)[] = new Array(n).fill(null);
    let ok = true;
    let d = w * 0.3;
    // A riff may use one string twice; the diagram can show one dot per string, so a repeated
    // string keeps its first note (the order label shows the sequence).
    seq.forEach((pc, idx) => {
      if (!ok) return;
      const exact = noteMidi(chord, idx);
      let best: { s: number; f: number } | null = null;
      for (let s = 0; s < n; s++) {
        for (let f = w === 0 ? 0 : w; f <= w + inst.span; f++) {
          if (exact !== null ? inst.tuning[s] + f !== exact : mod12(inst.tuning[s] + f) !== pc) continue;
          if (frets[s] !== null && frets[s] !== f) continue;
          if (!best || inst.tuning[s] + f < inst.tuning[best.s] + best.f) best = { s, f };
        }
      }
      if (!best) {
        ok = false;
        return;
      }
      if (frets[best.s] === null) {
        frets[best.s] = best.f;
        order[best.s] = idx + 1;
        fingers[best.s] = best.f === 0 ? null : Math.min(4, best.f - Math.max(w, 1) + 1);
      }
      d += best.s * 0.05;
    });
    if (!ok) continue;
    const key = frets.join();
    if (results.some((r) => r.frets.join() === key)) continue;
    results.push({ frets, fingers, barres: [], order, baseFret: baseFretOf(frets, inst.span), difficulty: Math.round(d * 100) / 100 });
  }
  return results.sort((a, b) => a.difficulty - b.difficulty).slice(0, 6);
}

/** MIDI notes a fretted voicing sounds, lowest first. */
export function voicingMidi(v: FretVoicing, instId: FrettedInstrument["id"], capo = 0): number[] {
  const inst = FRETTED[instId];
  const notes: number[] = [];
  v.frets.forEach((f, i) => {
    if (f !== null) notes.push(inst.tuning[i] + capo + f);
  });
  if (v.order) {
    // riff: in playing order
    return v.frets
      .map((f, i) => ({ f, i, o: v.order![i] }))
      .filter((x) => x.f !== null && x.o !== null)
      .sort((a, b) => a.o! - b.o!)
      .map((x) => inst.tuning[x.i] + capo + x.f!);
  }
  return notes.sort((a, b) => a - b);
}

// ---- piano ----

export interface PianoVoicing {
  /** right hand, MIDI, ascending */
  right: number[];
  /** left hand bass note (MIDI) or null */
  left: number | null;
  /** for riffs: order labels per MIDI note */
  order?: Map<number, number>;
  /** keyboard range to draw (inclusive) */
  from: number;
  to: number;
}

/** Right hand in C4–B4 (root position when it fits, else the inversion that does),
 *  left hand plays the bass (or slash bass) in C3–B3. */
export function pianoVoicing(symbol: string): PianoVoicing | null {
  const chord = parseChord(symbol);
  if (!chord || chord.category === "none") return null;
  if (chord.category === "note" || chord.category === "notes") {
    const seq = chord.category === "notes" ? chord.sequence ?? [] : [chord.root];
    const order = new Map<number, number>();
    const right: number[] = [];
    let prev = 60 + seq[0];
    seq.forEach((pc, i) => {
      // the written octave, else the octave nearest the previous note, kept within the drawn range
      let m = noteMidi(chord, i) ?? 48 + pc;
      if (noteMidi(chord, i) === null) while (m < prev - 6) m += 12;
      while (m < 48) m += 12;
      while (m > 83) m -= 12;
      if (!order.has(m)) order.set(m, i + 1);
      right.push(m);
      prev = m;
    });
    return { right: [...new Set(right)].sort((a, b) => a - b), left: null, order, from: 48, to: 83 };
  }
  const pcs = chord.intervals.map((i) => mod12(chord.root + i));
  const rootMidi = 60 + chord.root;
  const right = [...new Set(chord.intervals.map((i) => {
    let m = rootMidi + (i % 12);
    while (m > 71) m -= 12;
    return m;
  }))].sort((a, b) => a - b);
  // keep the 3rd / 7th, drop a doubled 5th if crowded: nothing to do in a one-octave close voicing
  const bassPc = chord.bass ?? chord.root;
  void pcs;
  return { right, left: 48 + bassPc, from: 48, to: 71 };
}

export function pianoMidi(v: PianoVoicing): number[] {
  return v.left !== null ? [v.left, ...v.right] : v.right;
}
