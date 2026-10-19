// Chord symbols: parsing, classification, spelling and transposition.
//
// Besides ordinary chords a symbol can be:
//   "N.C."          no chord
//   "note:E"        a single note to play
//   "notes:E G A"   a few single notes in a row (a riff)
//   "E5"            a power chord (root + fifth)

import {
  COMMON_NAMES,
  type Key,
  keyPrefersFlats,
  letterUp,
  mod12,
  noteName,
  parseKey,
  parseNote,
  parseNoteAt,
  prettyAccidentals,
  spellWithLetter,
} from "./notes";

export type ChordCategory =
  | "none" // N.C.
  | "note" // a single note
  | "notes" // a riff of single notes
  | "power" // root + 5th
  | "triad" // major, minor, diminished, augmented
  | "sus" // suspended 2nd / 4th
  | "added" // 6, add9, 6/9
  | "seventh" // any chord whose top extension is the 7th
  | "extended"; // 9, 11, 13, altered

export const CATEGORY_LABEL: Record<ChordCategory, string> = {
  none: "No chord",
  note: "Single note",
  notes: "Notes / riff",
  power: "Power chord",
  triad: "Triad",
  sus: "Suspended",
  added: "Added tone",
  seventh: "Seventh chord",
  extended: "Extended chord",
};

export interface Chord {
  category: ChordCategory;
  /** root pitch class (first note for "notes") */
  root: number;
  /** root as written, ASCII ("Bb") */
  rootName: string;
  /** canonical suffix: "", "m", "7", "maj7", "m7b5", "sus4", "5", "add9" ... */
  suffix: string;
  /** slash bass */
  bass?: number;
  bassName?: string;
  /** semitones above the root, sorted, root (0) included; 9ths etc. kept above 12 (14, 17, 21) */
  intervals: number[];
  /** chord formula, same order as intervals: "1", "b3", "5", "b7" */
  degrees: string[];
  /** degrees that may be left out when voicing (usually the 5th, and the 9th / 11th in 11 / 13 chords) */
  optional: number[];
  /** for "notes": every note's pitch class, in order */
  sequence?: number[];
  sequenceNames?: string[];
  /** for "note" / "notes": the octave of each note when written ("E4"), else null */
  octaves?: (number | null)[];
  /** e.g. "B♭ minor seventh" */
  description: string;
}

interface Quality {
  intervals: number[];
  degrees: string[];
  optional: number[];
  category: ChordCategory;
  description: string;
}

// Canonical suffix -> what it means. Aliases are mapped onto these in normaliseSuffix().
const QUALITIES: Record<string, Quality> = {};

function q(suffix: string, category: ChordCategory, description: string, degrees: string[], optional: string[] = []) {
  const intervals = degrees.map(degreeSemitones);
  QUALITIES[suffix] = {
    intervals,
    degrees,
    optional: optional.map(degreeSemitones),
    category,
    description,
  };
}

const DEGREE: Record<string, number> = {
  "1": 0, b2: 1, "2": 2, "#2": 3, b3: 3, "3": 4, "4": 5, "#4": 6, b5: 6, "5": 7, "#5": 8, b6: 8,
  "6": 9, bb7: 9, b7: 10, "7": 11, b9: 13, "9": 14, "#9": 15, "11": 17, "#11": 18, b13: 20, "13": 21,
};
function degreeSemitones(d: string): number {
  const v = DEGREE[d];
  if (v === undefined) throw new Error("unknown degree " + d);
  return v;
}

q("", "triad", "major", ["1", "3", "5"]);
q("m", "triad", "minor", ["1", "b3", "5"]);
q("dim", "triad", "diminished", ["1", "b3", "b5"]);
q("aug", "triad", "augmented", ["1", "3", "#5"]);
q("5", "power", "power chord", ["1", "5"]);
q("sus2", "sus", "suspended 2nd", ["1", "2", "5"]);
q("sus4", "sus", "suspended 4th", ["1", "4", "5"]);
q("6", "added", "sixth", ["1", "3", "5", "6"], ["5"]);
q("m6", "added", "minor sixth", ["1", "b3", "5", "6"], ["5"]);
q("69", "added", "six-nine", ["1", "3", "5", "6", "9"], ["5"]);
q("m69", "added", "minor six-nine", ["1", "b3", "5", "6", "9"], ["5"]);
q("add9", "added", "added ninth", ["1", "3", "5", "9"], ["5"]);
q("madd9", "added", "minor added ninth", ["1", "b3", "5", "9"], ["5"]);
q("add11", "added", "added eleventh", ["1", "3", "5", "11"], ["5"]);
q("7", "seventh", "dominant seventh", ["1", "3", "5", "b7"], ["5"]);
q("maj7", "seventh", "major seventh", ["1", "3", "5", "7"], ["5"]);
q("m7", "seventh", "minor seventh", ["1", "b3", "5", "b7"], ["5"]);
q("m7b5", "seventh", "half-diminished seventh", ["1", "b3", "b5", "b7"]);
q("dim7", "seventh", "diminished seventh", ["1", "b3", "b5", "bb7"]);
q("mmaj7", "seventh", "minor-major seventh", ["1", "b3", "5", "7"], ["5"]);
q("7sus4", "seventh", "dominant seventh, suspended 4th", ["1", "4", "5", "b7"], ["5"]);
q("7sus2", "seventh", "dominant seventh, suspended 2nd", ["1", "2", "5", "b7"], ["5"]);
q("aug7", "seventh", "augmented seventh", ["1", "3", "#5", "b7"]);
q("maj7#5", "seventh", "augmented major seventh", ["1", "3", "#5", "7"]);
q("7b5", "seventh", "seventh, flat five", ["1", "3", "b5", "b7"]);
q("9", "extended", "dominant ninth", ["1", "3", "5", "b7", "9"], ["5"]);
q("maj9", "extended", "major ninth", ["1", "3", "5", "7", "9"], ["5"]);
q("m9", "extended", "minor ninth", ["1", "b3", "5", "b7", "9"], ["5"]);
q("9sus4", "extended", "ninth, suspended 4th", ["1", "4", "5", "b7", "9"], ["5"]);
q("7b9", "extended", "seventh, flat nine", ["1", "3", "5", "b7", "b9"], ["5"]);
q("7#9", "extended", "seventh, sharp nine", ["1", "3", "5", "b7", "#9"], ["5"]);
q("7#11", "extended", "seventh, sharp eleven", ["1", "3", "5", "b7", "#11"], ["5"]);
q("maj7#11", "extended", "major seventh, sharp eleven", ["1", "3", "5", "7", "#11"], ["5"]);
q("11", "extended", "eleventh", ["1", "3", "5", "b7", "9", "11"], ["3", "5", "9"]);
q("m11", "extended", "minor eleventh", ["1", "b3", "5", "b7", "9", "11"], ["5", "9"]);
q("13", "extended", "thirteenth", ["1", "3", "5", "b7", "9", "13"], ["5", "9"]);
q("maj13", "extended", "major thirteenth", ["1", "3", "5", "7", "9", "13"], ["5", "9"]);
q("m13", "extended", "minor thirteenth", ["1", "b3", "5", "b7", "9", "13"], ["5", "9"]);

/** Common ways of writing a suffix -> the canonical one. */
const ALIASES: [RegExp, string][] = [
  [/^(maj|ma|M|Δ|major)$/, ""],
  [/^(min|mi|-|minor)$/, "m"],
  [/^(°|o|dim|diminished)$/, "dim"],
  [/^(\+|aug|augmented|\(#5\)|#5)$/, "aug"],
  [/^(sus)$/, "sus4"],
  [/^(2)$/, "sus2"],
  [/^(4)$/, "sus4"],
  [/^(add2)$/, "add9"],
  [/^(m\(?add9\)?|madd2|-add9|minadd9)$/, "madd9"],
  [/^(\(add9\))$/, "add9"],
  [/^(6\/9|6add9|69)$/, "69"],
  [/^(m6\/9|m69|-69|-6\/9)$/, "m69"],
  [/^(min6|-6|mi6)$/, "m6"],
  [/^(dom7|7)$/, "7"],
  [/^(maj7|M7|Δ7|Δ|ma7|j7|major7)$/, "maj7"],
  [/^(min7|-7|mi7|m7)$/, "m7"],
  [/^(ø|ø7|m7b5|m7\(b5\)|-7b5|min7b5|mi7b5)$/, "m7b5"],
  [/^(°7|o7|dim7)$/, "dim7"],
  [/^(m\(?maj7\)?|mM7|m\(M7\)|-maj7|minmaj7|mΔ7|-Δ7|mmaj7)$/, "mmaj7"],
  [/^(7sus|7sus4|sus7)$/, "7sus4"],
  [/^(\+7|7\+|7#5|aug7|7aug)$/, "aug7"],
  [/^(maj9|M9|Δ9|ma9)$/, "maj9"],
  [/^(min9|-9|m9|mi9)$/, "m9"],
  [/^(9sus|9sus4)$/, "9sus4"],
  [/^(maj13|M13|Δ13|ma13)$/, "maj13"],
  [/^(min13|-13|m13)$/, "m13"],
  [/^(min11|-11|m11)$/, "m11"],
  [/^(7\(b9\))$/, "7b9"],
  [/^(7\(#9\))$/, "7#9"],
  [/^(7\(b5\))$/, "7b5"],
];

export function normaliseSuffix(raw: string): string | null {
  const s = raw.replace(/\s+/g, "");
  if (s in QUALITIES) return s;
  for (const [re, canon] of ALIASES) if (re.test(s)) return canon;
  // "mmaj7" written as "mMaj7"
  const lower = s.replace(/Maj/g, "maj");
  if (lower in QUALITIES) return lower;
  return null;
}

const cache = new Map<string, Chord | null>();

/** Parses a chord symbol. Returns null when it isn't one. */
export function parseChord(symbol: string): Chord | null {
  const key = symbol.trim();
  if (cache.has(key)) return cache.get(key)!;
  const chord = parseUncached(key);
  if (cache.size > 5000) cache.clear();
  cache.set(key, chord);
  return chord;
}

function parseUncached(s: string): Chord | null {
  if (!s) return null;
  if (/^(N\.?C\.?|NC|-|x|%)$/i.test(s)) {
    return { category: "none", root: 0, rootName: "", suffix: "", intervals: [], degrees: [], optional: [], description: "no chord" };
  }
  const noteMatch = /^note:\s*(.+)$/i.exec(s);
  if (noteMatch) {
    const n = parseNoteWithOctave(noteMatch[1].trim());
    if (!n) return null;
    return {
      category: "note", root: n.pc, rootName: n.name, suffix: "", intervals: [0], degrees: ["1"], optional: [],
      octaves: [n.octave], description: `single note ${prettyAccidentals(n.name)}${n.octave ?? ""}`,
    };
  }
  const notesMatch = /^notes:\s*(.+)$/i.exec(s);
  if (notesMatch) {
    const parsed = notesMatch[1].trim().split(/[\s,]+/).map(parseNoteWithOctave);
    if (!parsed.length || parsed.some((p) => !p)) return null;
    const notes = parsed as NonNullable<(typeof parsed)[number]>[];
    return {
      category: "notes", root: notes[0].pc, rootName: notes[0].name, suffix: "",
      intervals: [0], degrees: ["1"], optional: [],
      sequence: notes.map((n) => n.pc), sequenceNames: notes.map((n) => n.name), octaves: notes.map((n) => n.octave),
      description: `notes ${notes.map((n) => prettyAccidentals(n.name)).join(" ")}`,
    };
  }

  const root = parseNoteAt(s);
  if (!root) return null;
  let rest = s.slice(root.length);
  let bass: ReturnType<typeof parseNoteAt> = null;
  // slash bass, but not "6/9"
  const slash = rest.lastIndexOf("/");
  if (slash >= 0 && !/^6\/9|m6\/9|-6\/9$/.test(rest.slice(Math.max(0, slash - 2), slash + 2))) {
    const b = parseNote(rest.slice(slash + 1));
    if (b) {
      bass = b;
      rest = rest.slice(0, slash);
    } else if (!/6\/9$/.test(rest)) {
      return null;
    }
  }
  const suffix = normaliseSuffix(rest);
  if (suffix === null) return null;
  const quality = QUALITIES[suffix];
  const rootPretty = prettyAccidentals(root.name);
  const chord: Chord = {
    category: quality.category,
    root: root.pc,
    rootName: root.name,
    suffix,
    intervals: quality.intervals,
    degrees: quality.degrees,
    optional: quality.optional,
    description: `${rootPretty} ${quality.description}`,
  };
  if (bass && bass.pc !== root.pc) {
    chord.bass = bass.pc;
    chord.bassName = bass.name;
    chord.description += ` over ${prettyAccidentals(bass.name)}`;
  }
  return chord;
}

/** "E4", "F#3", "Bb" (no octave) */
function parseNoteWithOctave(t: string): { pc: number; name: string; octave: number | null } | null {
  const m = /^(.*?)(-?\d)?$/.exec(t);
  if (!m) return null;
  const n = parseNote(m[1]);
  if (!n) return null;
  return { pc: n.pc, name: n.name, octave: m[2] !== undefined ? parseInt(m[2], 10) : null };
}

/** MIDI number of note i of a note / riff symbol, when its octave is written. */
export function noteMidi(c: Chord, i = 0): number | null {
  const oct = c.octaves?.[i];
  if (oct === null || oct === undefined) return null;
  const pc = c.category === "notes" ? c.sequence![i] : c.root;
  // spelling across the octave line: Cb4 is B3, B#3 is C4
  const name = c.category === "notes" ? c.sequenceNames![i] : c.rootName;
  const letterShift = name.startsWith("Cb") ? -12 : name.startsWith("B#") ? 12 : 0;
  return (oct + 1) * 12 + pc + letterShift;
}

export function isChordSymbol(s: string): boolean {
  return parseChord(s) !== null;
}

/** Is it a slash chord / inversion? */
export const isSlash = (c: Chord) => c.bass !== undefined;

/** Pitch classes in the chord (bass first when it is a slash chord). */
export function chordPitchClasses(c: Chord): number[] {
  if (c.category === "notes") return [...new Set(c.sequence)];
  const pcs = c.intervals.map((i) => mod12(c.root + i));
  if (c.bass !== undefined && !pcs.includes(c.bass)) pcs.unshift(c.bass);
  return [...new Set(pcs)];
}

/** Note names of the chord tones, spelled from the root letter (Gb major: Gb Bb Db). */
export function chordNoteNames(c: Chord): string[] {
  if (c.category === "none") return [];
  if (c.category === "notes") return c.sequenceNames ?? [];
  const rootLetter = c.rootName[0];
  const names = c.intervals.map((iv, i) => {
    const deg = c.degrees[i].replace(/[b#]/g, "");
    const steps = (parseInt(deg, 10) - 1) % 7;
    const pc = mod12(c.root + iv);
    return spellWithLetter(pc, letterUp(rootLetter, steps)) ?? noteName(pc, c.rootName.includes("b"));
  });
  // Double flats / sharps are correct but hard to read: show the plain enharmonic.
  const flats = c.rootName.includes("b") || c.rootName === "F";
  const plain = names.map((n, i) => (/bb|##/.test(n) ? noteName(mod12(c.root + c.intervals[i]), flats) : n));
  if (c.bassName && !plain.includes(c.bassName)) plain.unshift(c.bassName);
  return plain;
}

// ---- spelling / transposition ----

export type Spelling = "flats" | "sharps" | "common";

export function spellPc(pc: number, spelling: Spelling): string {
  if (spelling === "common") return COMMON_NAMES[mod12(pc)];
  return noteName(pc, spelling === "flats");
}

/** Writes a chord back as a symbol (canonical suffix). */
export function chordSymbol(c: Chord, rootName = c.rootName, bassName = c.bassName): string {
  if (c.category === "none") return "N.C.";
  if (c.category === "note") return `note:${rootName}${c.octaves?.[0] ?? ""}`;
  if (c.category === "notes") return `notes:${(c.sequenceNames ?? []).map((n, i) => n + (c.octaves?.[i] ?? "")).join(" ")}`;
  return rootName + c.suffix + (c.bass !== undefined && bassName ? "/" + bassName : "");
}

/** Transposes a chord symbol by `semitones`, spelling roots for the target key.
 *  0 semitones returns the symbol unchanged (keeps the author's spelling). */
export function transposeSymbol(symbol: string, semitones: number, spelling: Spelling): string {
  const c = parseChord(symbol);
  if (!c || c.category === "none") return symbol;
  if (semitones === 0) return symbol;
  if (c.category === "note" || c.category === "notes") {
    const seq = c.category === "notes" ? c.sequence ?? [] : [c.root];
    const moved = seq.map((pc, i) => {
      const midi = noteMidi(c, i);
      const name = spellPc(pc + semitones, spelling);
      if (midi === null) return name;
      const m = midi + semitones;
      // octave numbers follow the letter: Cb5 sounds as B4
      const oct = Math.floor(m / 12) - 1 + (name.startsWith("Cb") ? 1 : name.startsWith("B#") ? -1 : 0);
      return name + oct;
    });
    return c.category === "note" ? "note:" + moved[0] : "notes:" + moved.join(" ");
  }
  if (mod12(semitones) === 0) return symbol;
  let root = spellPc(c.root + semitones, spelling);
  // guitarists call the minor chord on this shape G#m, not Abm
  if (spelling === "common" && root === "Ab" && c.intervals.includes(3) && !c.intervals.includes(4)) root = "G#";
  const bass = c.bass !== undefined ? spellPc(c.bass + semitones, spelling) : undefined;
  return chordSymbol(c, root, bass);
}

/** Spelling to use for chords in `keyName` transposed by `semitones`. */
export function spellingForKey(keyName: string, semitones: number): Spelling {
  const key = parseKey(keyName);
  if (!key) return "common";
  const t: Key = { tonic: mod12(key.tonic + semitones), minor: key.minor, name: "" };
  t.name = (key.minor ? MINOR_KEY_NAMES : MAJOR_KEY_NAMES)[t.tonic];
  return keyPrefersFlats(t) ? "flats" : "sharps";
}
const MAJOR_KEY_NAMES = ["C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"];
const MINOR_KEY_NAMES = ["Cm", "C#m", "Dm", "Ebm", "Em", "Fm", "F#m", "Gm", "G#m", "Am", "Bbm", "Bm"];

// ---- simplify ----

/** An easier chord with the same function: drops 7ths / extensions / added tones / slash bass.
 *  Power chords, single notes and plain triads are kept. */
export function simplifySymbol(symbol: string): string {
  const c = parseChord(symbol);
  if (!c || c.category === "none" || c.category === "note" || c.category === "notes" || c.category === "power") return symbol;
  const minorish = c.intervals.includes(3) && !c.intervals.includes(4);
  let suffix = minorish ? "m" : "";
  if (c.suffix === "dim" || c.suffix === "dim7" || c.suffix === "m7b5") suffix = "dim";
  if (c.suffix === "aug" || c.suffix === "aug7") suffix = "aug";
  return c.rootName + suffix;
}

// ---- display ----

export interface ChordDisplay {
  root: string; // "B♭"
  quality: string; // "m", "dim", "sus", "" (normal size)
  ext: string; // "7", "maj7", "9", "add9" (superscript)
  bass: string; // "/F♯" or ""
  text: string; // whole thing as plain text
}

/** Splits a chord symbol for display: root, quality, superscript extension, bass. */
export function displayChord(symbol: string): ChordDisplay {
  const c = parseChord(symbol);
  if (!c) return { root: symbol, quality: "", ext: "", bass: "", text: symbol };
  if (c.category === "none") return { root: "N.C.", quality: "", ext: "", bass: "", text: "N.C." };
  if (c.category === "note") {
    const r = prettyAccidentals(c.rootName);
    return { root: r, quality: "", ext: "", bass: "", text: r };
  }
  if (c.category === "notes") {
    const t = (c.sequenceNames ?? []).map(prettyAccidentals).join(" ");
    return { root: t, quality: "", ext: "", bass: "", text: t };
  }
  const root = prettyAccidentals(c.rootName);
  let quality = "";
  let ext = c.suffix;
  const m = /^(m(?!aj)|dim|aug|sus)/.exec(c.suffix);
  if (m && c.suffix !== "mmaj7") {
    quality = m[1];
    ext = c.suffix.slice(m[1].length);
  }
  if (c.suffix === "mmaj7") {
    quality = "m";
    ext = "maj7";
  }
  if (c.suffix === "sus2" || c.suffix === "sus4") {
    quality = "sus";
    ext = c.suffix.slice(3);
  }
  if (c.suffix === "5") ext = "5";
  const bass = c.bassName ? "/" + prettyAccidentals(c.bassName) : "";
  return { root, quality, ext, bass, text: root + quality + ext + bass };
}

/** Roman-numeral-ish signature of a chord relative to a key, for "similar songs":
 *  (root - tonic) mod 12 + suffix family. */
export function chordFunction(symbol: string, keyTonic: number): string | null {
  const c = parseChord(symbol);
  if (!c || c.category === "none") return null;
  const family = c.intervals.includes(3) && !c.intervals.includes(4) ? "m" : c.category === "power" ? "5" : "";
  return mod12(c.root - keyTonic) + family;
}
