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

// Number of sharps (+) or flats (-) in each major key, by tonic spelling.
const MAJOR_SIGNATURE: Record<string, number> = {
  C: 0, G: 1, D: 2, A: 3, E: 4, B: 5, "F#": 6, "C#": 7,
  F: -1, Bb: -2, Eb: -3, Ab: -4, Db: -5, Gb: -6, Cb: -7,
};

/** Key signature of a key: sharps > 0, flats < 0. Unknown spellings (e.g. "D#") return the
 *  enharmonic's. */
export function keySignature(key: Key): number {
  // A minor key shares the signature of the major key a minor third up.
  const majorPc = key.minor ? mod12(key.tonic + 3) : key.tonic;
  const majorName = key.minor ? relativeMajorName(key) : key.name;
  if (majorName in MAJOR_SIGNATURE) return MAJOR_SIGNATURE[majorName];
  return MAJOR_SIGNATURE[DEFAULT_MAJOR[majorPc]];
}

function relativeMajorName(key: Key): string {
  const root = key.name.replace(/m$/, "");
  const n = parseNote(root);
  if (!n) return DEFAULT_MAJOR[mod12(key.tonic + 3)];
  return spellWithLetter(mod12(key.tonic + 3), letterUp(n.letter, 2)) ?? DEFAULT_MAJOR[mod12(key.tonic + 3)];
}

/** The usual spelling of each major / minor key (fewest accidentals; F# over Gb on a tie). */
export const DEFAULT_MAJOR = ["C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"];
export const DEFAULT_MINOR = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "G#", "A", "Bb", "B"];

/** Does music in this key normally use flats for accidentals? C major / A minor lean flat
 *  (Bb, Eb and Ab chords are more common there than A#, D#, G#). */
export function keyPrefersFlats(key: Key | null): boolean {
  if (!key) return true;
  const sig = keySignature(key);
  return sig <= 0;
}

/** The key `semitones` above `key`, spelled the usual way. */
export function transposeKey(key: Key, semitones: number): Key {
  if (mod12(semitones) === 0) return key;
  const tonic = mod12(key.tonic + semitones);
  const root = (key.minor ? DEFAULT_MINOR : DEFAULT_MAJOR)[tonic];
  return { tonic, minor: key.minor, name: root + (key.minor ? "m" : "") };
}

/** "Bb" -> "B♭", "F#" -> "F♯" for display. */
export function prettyAccidentals(s: string): string {
  return s.replace(/([A-G])bb/g, "$1𝄫").replace(/([A-G])##/g, "$1𝄪").replace(/([A-G])b/g, "$1♭").replace(/([A-G])#/g, "$1♯");
}

/** MIDI note number -> "C4" style name. */
export function midiName(midi: number, flats = false): string {
  return noteName(midi, flats) + (Math.floor(midi / 12) - 1);
}

export function midiToFreq(midi: number): number {
  return 440 * Math.pow(2, (midi - 69) / 12);
}
