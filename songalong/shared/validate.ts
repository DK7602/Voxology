// Validation of data the browser sends: song documents, practice settings and user settings.
// Shared so the app can run the same checks before saving. Every function returns a fresh,
// clean object (unknown fields dropped) or throws a ValidationError with a user-facing message.

import type {
  ChordEvent,
  Instrument,
  LyricLine,
  Media,
  PracticeSettings,
  SongDoc,
  UserSettings,
} from "./types";
import { validBarFixes, validDrums, validFixes } from "./music/cajon";
import { wordStarts } from "./timeline";

export class ValidationError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "ValidationError";
  }
}

export const SONG_LIMITS = {
  title: 200,
  artist: 200,
  genre: 60,
  key: 12,
  tuning: 60,
  chordName: 40,
  lineText: 500,
  section: 60,
  mediaName: 200,
  audioKey: 300,
  chords: 5000,
  lines: 2000,
  beatTimes: 20000,
  minBeat: -16,
} as const;

const INSTRUMENTS: readonly Instrument[] = ["guitar", "ukulele", "piano", "mandolin", "bass", "cajon"];
const THEMES = ["light", "dark", "system"] as const;
const PALETTES = ["gold", "usyay", "sunday"] as const;
const YOUTUBE_ID = /^[A-Za-z0-9_-]{11}$/;

function fail(message: string): never {
  throw new ValidationError(message);
}

function isObject(v: unknown): v is Record<string, unknown> {
  return typeof v === "object" && v !== null && !Array.isArray(v);
}

const clamp = (n: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, n));

/** Single-line text: control characters become spaces, runs of spaces collapse, ends trimmed. */
function cleanLine(s: string, max: number): string {
  return s.replace(/[\u0000-\u001f\u007f]+/g, " ").replace(/\s+/g, " ").trim().slice(0, max);
}

function optText(obj: Record<string, unknown>, field: string, label: string, max: number, fallback: string): string {
  const v = obj[field];
  if (v === undefined || v === null) return fallback;
  if (typeof v !== "string") fail(`${label} must be text`);
  return cleanLine(v, max);
}

function optNumber(
  obj: Record<string, unknown>,
  field: string,
  label: string,
  fallback: number,
  lo: number,
  hi: number,
  integer = false,
): number {
  const v = obj[field];
  if (v === undefined || v === null) return fallback;
  if (typeof v !== "number" || !Number.isFinite(v)) fail(`${label} must be a number`);
  return clamp(integer ? Math.round(v) : v, lo, hi);
}

function optBoolean(obj: Record<string, unknown>, field: string, label: string): boolean | undefined {
  const v = obj[field];
  if (v === undefined || v === null) return undefined;
  if (typeof v !== "boolean") fail(`${label} must be true or false`);
  return v;
}

function optInstrument(obj: Record<string, unknown>, field: string): Instrument | undefined {
  const v = obj[field];
  if (v === undefined || v === null) return undefined;
  if (!INSTRUMENTS.includes(v as Instrument)) fail("Unknown instrument");
  return v as Instrument;
}

function isBeat(v: unknown): v is number {
  return typeof v === "number" && Number.isFinite(v) && v >= SONG_LIMITS.minBeat;
}

function validateMedia(v: unknown): Media {
  if (v === undefined || v === null) return { kind: "none" };
  if (!isObject(v)) fail("Media must be an object");
  switch (v.kind) {
    case "none":
      return { kind: "none" };
    case "youtube": {
      const id = typeof v.videoId === "string" ? v.videoId.trim() : "";
      if (!YOUTUBE_ID.test(id)) fail("That doesn't look like a valid YouTube video");
      return { kind: "youtube", videoId: id };
    }
    case "audio": {
      const key = typeof v.key === "string" ? v.key.trim() : "";
      if (!key || key.length > SONG_LIMITS.audioKey) fail("The song's audio file is missing");
      const media: Media = {
        kind: "audio",
        key,
        name: optText(v, "name", "Audio file name", SONG_LIMITS.mediaName, "") || "Audio",
      };
      if (typeof v.duration === "number" && Number.isFinite(v.duration) && v.duration >= 0) {
        media.duration = v.duration;
      }
      return media;
    }
    default:
      fail("Unknown media type");
  }
}

function validateChords(v: unknown, what = "Chord"): ChordEvent[] {
  if (v === undefined || v === null) return [];
  if (!Array.isArray(v)) fail(`${what}s must be a list`);
  if (v.length > SONG_LIMITS.chords) fail(`A song can have at most ${SONG_LIMITS.chords} ${what.toLowerCase()}s`);
  const out: ChordEvent[] = [];
  v.forEach((c, i) => {
    if (!isObject(c)) fail(`${what} ${i + 1} is not valid`);
    if (!isBeat(c.beat)) fail(`${what} ${i + 1} has an invalid beat`);
    if (typeof c.name !== "string") fail(`${what} ${i + 1} needs a name`);
    const name = cleanLine(c.name, SONG_LIMITS.chordName);
    if (!name) return;
    const chord: ChordEvent = { beat: c.beat, name };
    if (typeof c.col === "number" && Number.isFinite(c.col) && c.col >= 0) {
      chord.col = Math.min(Math.round(c.col), SONG_LIMITS.lineText);
    }
    out.push(chord);
  });
  return out.sort((a, b) => a.beat - b.beat);
}

function validateLines(v: unknown): LyricLine[] {
  if (v === undefined || v === null) return [];
  if (!Array.isArray(v)) fail("Lyrics must be a list of lines");
  if (v.length > SONG_LIMITS.lines) fail(`A song can have at most ${SONG_LIMITS.lines} lyric lines`);
  const out: LyricLine[] = v.map((l, i) => {
    if (!isObject(l)) fail(`Lyric line ${i + 1} is not valid`);
    if (!isBeat(l.beat)) fail(`Lyric line ${i + 1} has an invalid beat`);
    if (l.text !== undefined && l.text !== null && typeof l.text !== "string") {
      fail(`Lyric line ${i + 1} must be text`);
    }
    // Only the end is trimmed: leading spaces matter because chord `col`s index into the text.
    const text = (typeof l.text === "string" ? l.text : "")
      .replace(/\t/g, " ")
      .replace(/[\u0000-\u001f\u007f]+/g, "")
      .trimEnd()
      .slice(0, SONG_LIMITS.lineText);
    const line: LyricLine = { beat: l.beat, text };
    if (typeof l.section === "string") {
      const section = cleanLine(l.section, SONG_LIMITS.section);
      if (section) line.section = section;
    }
    if (typeof l.end === "number" && Number.isFinite(l.end) && l.end > l.beat) line.end = l.end;
    const words = validWords(l.words, text);
    if (words) {
      line.words = words;
      const last = words[words.length - 1];
      if (typeof l.wordsEnd === "number" && Number.isFinite(l.wordsEnd) && l.wordsEnd >= last) line.wordsEnd = round2(l.wordsEnd);
    }
    return line;
  });
  return out.sort((a, b) => a.beat - b.beat);
}

const round2 = (x: number) => Math.round(x * 100) / 100;

/** Word times kept only when there is one per word, in order. */
function validWords(v: unknown, text: string): number[] | undefined {
  if (!Array.isArray(v) || !v.length || v.length !== wordStarts(text).length) return undefined;
  if (!v.every((t) => typeof t === "number" && Number.isFinite(t))) return undefined;
  const words = (v as number[]).map(round2);
  return words.every((t, i) => i === 0 || t >= words[i - 1]) ? words : undefined;
}

function validateBeatTimes(v: unknown): number[] | undefined {
  if (v === undefined || v === null) return undefined;
  if (!Array.isArray(v)) fail("Beat times must be a list of numbers");
  if (v.length > SONG_LIMITS.beatTimes) fail("Too many beat times");
  for (const t of v) {
    if (typeof t !== "number" || !Number.isFinite(t)) fail("Beat times must be numbers");
  }
  return (v as number[]).slice().sort((a, b) => a - b);
}

/** Checks and normalises a song document from the client. */
export function validateSongDoc(input: unknown): SongDoc {
  if (!isObject(input)) fail("Song data must be an object");
  if (input.v !== undefined && input.v !== 1) fail("Unsupported song format version");

  const chords = validateChords(input.chords);
  const lead = validateChords(input.lead, "Lead note");
  const lines = validateLines(input.lines);
  const lastBeat = Math.max(
    0,
    chords.length ? chords[chords.length - 1].beat : 0,
    lead.length ? lead[lead.length - 1].beat : 0,
    ...lines.map((l) => l.end ?? l.beat),
  );

  const doc: SongDoc = {
    v: 1,
    title: optText(input, "title", "Title", SONG_LIMITS.title, "") || "Untitled",
    artist: optText(input, "artist", "Artist", SONG_LIMITS.artist, ""),
    genre: optText(input, "genre", "Genre", SONG_LIMITS.genre, ""),
    key: optText(input, "key", "Key", SONG_LIMITS.key, ""),
    bpm: optNumber(input, "bpm", "Tempo", 100, 20, 400),
    beatsPerBar: optNumber(input, "beatsPerBar", "Beats per bar", 4, 1, 12, true),
    capo: optNumber(input, "capo", "Capo", 0, 0, 12, true),
    tuning: optText(input, "tuning", "Tuning", SONG_LIMITS.tuning, "") || "standard",
    media: validateMedia(input.media),
    offset: optNumber(input, "offset", "Offset", 0, -86400, 86400),
    chords,
    lines,
    length: optNumber(input, "length", "Song length", lastBeat, 0, 1_000_000),
  };
  if (lead.length) doc.lead = lead;
  if (typeof input.credits === "string" && input.credits.trim()) doc.credits = cleanLine(input.credits, 200);
  const beatTimes = validateBeatTimes(input.beatTimes);
  if (beatTimes) doc.beatTimes = beatTimes;
  const drums = validDrums(input.drums);
  if (drums) doc.drums = drums;
  return doc;
}

const NO_CHORD = /^N\.?C\.?$/i;

/** Distinct chord names in order of first use (by beat), without "N.C.". */
export function distinctChords(doc: Pick<SongDoc, "chords">): string[] {
  const seen = new Set<string>();
  const sorted = doc.chords.slice().sort((a, b) => a.beat - b.beat);
  for (const c of sorted) {
    if (!NO_CHORD.test(c.name)) seen.add(c.name);
  }
  return [...seen];
}

/** The player's per-song state. Replaces the stored settings, so it is validated as a whole. */
export function validatePracticeSettings(input: unknown): PracticeSettings {
  if (input === undefined || input === null) return {};
  if (!isObject(input)) fail("Settings must be an object");
  const out: PracticeSettings = {};
  if (input.part === "rhythm" || input.part === "lead") out.part = input.part;
  // (the level kept as "cajon" before Easy was the default was mostly the old default: let go)
  if (input.cajonLevel === "easy" || input.cajonLevel === "groove") out.cajonLevel = input.cajonLevel;
  const fixes = validFixes(input.cajonFixes);
  if (fixes) out.cajonFixes = fixes;
  const barFixes = validBarFixes(input.cajonBarFixes);
  if (barFixes) out.cajonBarFixes = barFixes;
  if (input.transpose != null) out.transpose = optNumber(input, "transpose", "Transpose", 0, -12, 12, true);
  if (input.capo != null) out.capo = optNumber(input, "capo", "Capo", 0, 0, 12, true);
  const instrument = optInstrument(input, "instrument");
  if (instrument) out.instrument = instrument;
  if (input.speed != null) out.speed = optNumber(input, "speed", "Speed", 1, 0.5, 1.5);
  const simplify = optBoolean(input, "simplify", "Simplify");
  if (simplify !== undefined) out.simplify = simplify;
  if (input.chordStyle === "full" || input.chordStyle === "power") out.chordStyle = input.chordStyle;
  if (input.loop != null) {
    if (!isObject(input.loop)) fail("Loop must be an object");
    const loop: NonNullable<PracticeSettings["loop"]> = { on: optBoolean(input.loop, "on", "Loop") ?? false };
    if (isBeat(input.loop.from)) loop.from = input.loop.from;
    if (isBeat(input.loop.to)) loop.to = input.loop.to;
    out.loop = loop;
  }
  return out;
}

/** Applies a partial settings update to `base`. A field set to null is removed. */
export function validateUserSettings(input: unknown, base: UserSettings = {}): UserSettings {
  if (!isObject(input)) fail("Settings must be an object");
  const out: UserSettings = { ...base };
  const fields: (keyof UserSettings)[] = ["theme", "palette", "instrument", "leftHanded", "capoShapes", "countIn"];
  for (const k of fields) {
    if (!(k in input)) continue;
    if (input[k] === null) {
      delete out[k];
      continue;
    }
    switch (k) {
      case "theme":
        // the older "usyay" theme is USyay in light
        if (input.theme === "usyay") {
          out.theme = "light";
          if (!("palette" in input)) out.palette = "usyay";
          break;
        }
        if (!THEMES.includes(input.theme as (typeof THEMES)[number])) fail("Unknown theme");
        out.theme = input.theme as UserSettings["theme"];
        break;
      case "palette":
        if (!PALETTES.includes(input.palette as (typeof PALETTES)[number])) fail("Unknown color scheme");
        out.palette = input.palette as UserSettings["palette"];
        break;
      case "instrument":
        out.instrument = optInstrument(input, "instrument");
        break;
      case "leftHanded":
        out.leftHanded = optBoolean(input, "leftHanded", "Left-handed");
        break;
      case "capoShapes":
        out.capoShapes = optBoolean(input, "capoShapes", "Capo shapes");
        break;
      case "countIn":
        out.countIn = optNumber(input, "countIn", "Count-in", 0, 0, 8, true);
        break;
    }
  }
  return out;
}
