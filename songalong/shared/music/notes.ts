// Pitch classes, note spelling and keys.

export const SHARP_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
export const FLAT_NAMES = ["C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"];
/** How guitarists usually name chord roots (shape names after a capo). */
export const COMMON_NAMES = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"];

const LETTER_PC: Record<string, number> = { C: 0, D: 2, E: 4, F: 5, G: 7, A: 9, B: 11 };
const LETTERS = ["C", "D", "E", "F", "G", "A", "B"];

export const mod12 = (n: number) => ((n % 12) + 12) % 12;

export interface ParsedNote {
  pc: number;
  letter: string;
  /** -2 .. +2 */
  accidental: number;
  /** the note as written, normalised to ASCII (Bb, F#) */
  name: string;
  /** characters consumed */
  length: number;
}

/** Reads a note name at the start of `s`: C, C#, Db, F##, Bbb, also ♯ ♭. */
export function parseNoteAt(s: string): ParsedNote | null {
  const m = /^([A-Ga-g])(##|bb|#|b|♯♯|♭♭|♯|♭|𝄪|𝄫)?/.exec(s);
  if (!m) return null;
  const letter = m[1].toUpperCase();
  const acc = m[2] ?? "";
  let accidental = 0;
  for (const ch of acc) {
    if (ch === "#" || ch === "♯") accidental += 1;
    else if (ch === "b" || ch === "♭") accidental -= 1;
    else if (ch === "𝄪") accidental += 2;
    else if (ch === "𝄫") accidental -= 2;
  }
  const name = letter + (accidental > 0 ? "#".repeat(accidental) : "b".repeat(-accidental));
  return { pc: mod12(LETTER_PC[letter] + accidental), letter, accidental, name, length: m[0].length };
}

export function parseNote(s: string): ParsedNote | null {
  const n = parseNoteAt(s.trim());
  return n && n.length === s.trim().length ? n : null;
}

export function noteName(pc: number, flats: boolean): string {
  return (flats ? FLAT_NAMES : SHARP_NAMES)[mod12(pc)];
}

/** Spells `pc` with a given letter (for chord tones: the 3rd of Gb is Bb, not A#). Returns null when
 *  that would need more than two accidentals. */
export function spellWithLetter(pc: number, letter: string): string | null {
  let diff = mod12(pc - LETTER_PC[letter]);
  if (diff > 6) diff -= 12;
  if (Math.abs(diff) > 2) return null;
  return letter + (diff > 0 ? "#".repeat(diff) : "b".repeat(-diff));
}

/** The letter `steps` letters above `letter` (C + 2 -> E). */
export function letterUp(letter: string, steps: number): string {
  return LETTERS[(LETTERS.indexOf(letter) + steps + 70) % 7];
}

// ---- keys ----

export interface Key {
  tonic: number;
  minor: boolean;
  /** as written, e.g. "Gb", "Ebm" */
  name: string;
}

export function parseKey(s: string): Key | null {
  const t = s.trim().replace(/\s*(major|maj)$/i, "");
  const minorMatch = /^(.+?)\s*(m|min|minor|-)$/i.exec(t);
  const note = parseNote(minorMatch ? minorMatch[1] : t);
  if (!note) return null;
  const minor = !!minorMatch;
  return { tonic: note.pc, minor, name: note.name + (minor ? "m" : "") };
}

/** The usual name of each major / minor key (fewest accidentals; F# over Gb on a tie). */
export const DEFAULT_MAJOR = ["C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"];
export const DEFAULT_MINOR = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "G#", "A", "Bb", "B"];

/** A key by its usual name: F# rather than Gb, Db rather than C#, Ebm, G#m. */
export function usualKey(tonic: number, minor: boolean): Key {
  const t = mod12(tonic);
  return { tonic: t, minor, name: (minor ? DEFAULT_MINOR : DEFAULT_MAJOR)[t] + (minor ? "m" : "") };
}

/** The key `semitones` above `key`, by its usual name. */
export function transposeKey(key: Key, semitones: number): Key {
  if (mod12(semitones) === 0) return key;
  return usualKey(key.tonic + semitones, key.minor);
}

// ---- naming notes in a key ----
//
// Every note is named the way a printed chart in the key would: notes of the scale take the key's
// letters, and notes outside it the names players expect: flat 2nd, 3rd, 6th and 7th (Bb in G,
// Ab in C), sharp 4th (F# in C), and in minor keys the raised 3rd, 4th, 6th and 7th (G# in A minor).

/** letters above the tonic for each semitone above it */
const STEPS = [0, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6];
/** a note leading up a half step (a diminished chord's root) is the sharp of the note below:
 *  C#dim, D#dim, G#dim and A#dim in C */
const LEADING_MAJOR: Record<number, number> = { 1: 0, 3: 1, 8: 4, 10: 5 };
const LEADING_MINOR: Record<number, number> = { 1: 0 };
const NATURAL = new Set([0, 2, 4, 5, 7, 9, 11]);

/** `name` for `pc`, unless it is one players find hard to read (Cb, Fb, E#, B#, double sharps and
 *  flats): then the plain name, B rather than Cb, F# rather than E##. */
export function tidyName(pc: number, name: string | null): string {
  if (name && !/bb|##/.test(name) && !["Cb", "Fb", "E#", "B#"].includes(name)) return name;
  const p = mod12(pc);
  if (NATURAL.has(p)) return SHARP_NAMES[p];
  return noteName(p, !name?.includes("#"));
}

/** Names `pc` as it is written in `key`. `leading`: it leads up a half step (the root of a
 *  diminished chord). */
export function spellInKey(pc: number, key: Key, leading = false): string {
  const degree = mod12(pc - key.tonic);
  const letter = usualKey(key.tonic, key.minor).name[0];
  const steps = (leading ? (key.minor ? LEADING_MINOR : LEADING_MAJOR)[degree] : undefined) ?? STEPS[degree];
  return tidyName(pc, spellWithLetter(mod12(pc), letterUp(letter, steps)));
}

/** MIDI note number -> "F#4", named for `key` (sharps when there is no key). */
export function midiNameInKey(midi: number, key: Key | null): string {
  return (key ? spellInKey(midi, key) : noteName(midi, false)) + (Math.floor(midi / 12) - 1);
}

/** "Bb" -> "B♭", "F#" -> "F♯", "7b9" -> "7♭9" for display. */
export function prettyAccidentals(s: string): string {
  return s
    .replace(/([A-G])bb/g, "$1𝄫")
    .replace(/([A-G])##/g, "$1𝄪")
    .replace(/([A-G])b/g, "$1♭")
    .replace(/([A-G])#/g, "$1♯")
    // altered extensions and degrees: m7b5, 7#9, b3
    .replace(/(^|[^A-Za-z])b(?=\d)/g, "$1♭")
    .replace(/(^|[^A-Za-z])#(?=\d)/g, "$1♯");
}

/** MIDI note number -> "C4" style name. */
export function midiName(midi: number, flats = false): string {
  return noteName(midi, flats) + (Math.floor(midi / 12) - 1);
}

export function midiToFreq(midi: number): number {
  return 440 * Math.pow(2, (midi - 69) / 12);
}
