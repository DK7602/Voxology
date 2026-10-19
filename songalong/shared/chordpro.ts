// Import / export of chord sheets as text.
//
// Import understands both common formats:
//   ChordPro:              {title: ...}  [G]Words with [D]chords inline
//   chords over lyrics:    a line of chords above a line of words
// Timing is not in these formats, so it is guessed (one bar per chord, two bars for a line with
// a single chord); the play-along editor's tap-to-sync fixes it against the recording.

import { isChordSymbol } from "./music/chords";
import { emptySong, type ChordEvent, type LyricLine, type SongDoc } from "./types";
import { chordsInLine } from "./timeline";

interface ParsedLine {
  section?: string;
  text: string;
  chords: { name: string; col: number }[];
}

const SECTION_WORDS =
  /^(intro|verse|pre-?chorus|chorus|bridge|outro|interlude|solo|instrumental|hook|refrain|tag|coda|breakdown|ending|riff)\b/i;

function directive(line: string): [string, string] | null {
  const m = /^\{\s*([a-z_]+)\s*(?::\s*(.*?))?\s*\}$/i.exec(line.trim());
  return m ? [m[1].toLowerCase(), m[2] ?? ""] : null;
}

/** Is this a line made only of chord symbols (and bar lines / repeat marks)? */
function chordLineTokens(line: string): { name: string; col: number }[] | null {
  if (!line.trim()) return null;
  const tokens: { name: string; col: number }[] = [];
  const re = /\S+/g;
  let m: RegExpExecArray | null;
  let chordCount = 0;
  while ((m = re.exec(line))) {
    const tok = m[0];
    if (/^(\||\|\||:\|\||\|\|:|\/|-|x\d+|\(x\d+\)|\d+x|%)$/i.test(tok)) continue;
    const bare = tok.replace(/^\(|\)$/g, "");
    if (!isChordSymbol(bare)) return null;
    tokens.push({ name: bare === "NC" || bare === "N.C" ? "N.C." : bare, col: m.index });
    chordCount++;
  }
  return chordCount ? tokens : null;
}

function sectionHeading(line: string): string | null {
  const t = line.trim();
  const bracket = /^\[([^\]]+)\]:?$/.exec(t);
  if (bracket && !isChordSymbol(bracket[1]) && SECTION_WORDS.test(bracket[1])) return bracket[1].trim();
  if (/^[A-Za-z][A-Za-z -]*\d*:?$/.test(t) && SECTION_WORDS.test(t) && t.split(/\s+/).length <= 3) {
    return t.replace(/:$/, "");
  }
  return null;
}

/** "[G]You know I [D]still..." -> text + chord columns */
function inlineChords(line: string): ParsedLine | null {
  if (!/\[[^\]]+\]/.test(line)) return null;
  let text = "";
  const chords: ParsedLine["chords"] = [];
  const re = /\[([^\]]+)\]/g;
  let last = 0;
  let m: RegExpExecArray | null;
  while ((m = re.exec(line))) {
    text += line.slice(last, m.index);
    const name = m[1].trim();
    if (!isChordSymbol(name)) return null;
    chords.push({ name, col: text.length });
    last = m.index + m[0].length;
  }
  text += line.slice(last);
  return { text: text.replace(/\s+$/, ""), chords };
}

export interface ImportResult {
  doc: SongDoc;
  /** lines that could not be understood */
  warnings: string[];
}

export function importChordSheet(input: string, base: Partial<SongDoc> = {}): ImportResult {
  const doc: SongDoc = { ...emptySong(), ...base };
  const warnings: string[] = [];
  const parsed: ParsedLine[] = [];
  let pendingSection: string | undefined;
  const raw = input.replace(/\r\n?/g, "\n").split("\n");

  for (let i = 0; i < raw.length; i++) {
    const line = raw[i].replace(/\t/g, "    ").replace(/\s+$/, "");
    if (!line.trim()) continue;
    const dir = directive(line);
    if (dir) {
      const [name, value] = dir;
      if (name === "title" || name === "t") doc.title = value;
      else if (name === "artist" || name === "subtitle" || name === "st" || name === "a") doc.artist = doc.artist || value;
      else if (name === "key") doc.key = value || doc.key;
      else if (name === "tempo" || name === "bpm") doc.bpm = parseFloat(value) || doc.bpm;
      else if (name === "capo") doc.capo = parseInt(value, 10) || 0;
      else if (name === "time") {
        const n = parseInt(value, 10);
        if (n > 0 && n <= 12) doc.beatsPerBar = n;
      } else if (name === "genre" || name === "meta" && /^genre\s/.test(value)) doc.genre = value.replace(/^genre\s+/, "");
      else if (name === "comment" || name === "c" || name === "ci" || name === "comment_italic") pendingSection = value;
      else if (/^(start_of_|so)(chorus|c)$/.test(name) || name === "soc") pendingSection = value || "Chorus";
      else if (/^(start_of_|so)(verse|v)$/.test(name) || name === "sov") pendingSection = value || "Verse";
      else if (/^(start_of_|so)(bridge|b)$/.test(name) || name === "sob") pendingSection = value || "Bridge";
      // end_of_*, other directives: nothing to do
      continue;
    }
    if (line.trim().startsWith("#")) continue;
    const heading = sectionHeading(line);
    if (heading) {
      pendingSection = heading;
      continue;
    }
    const inline = inlineChords(line);
    if (inline) {
      parsed.push({ ...inline, section: pendingSection });
      pendingSection = undefined;
      continue;
    }
    const chordTokens = chordLineTokens(line);
    if (chordTokens) {
      const next = raw[i + 1]?.replace(/\t/g, "    ").replace(/\s+$/, "") ?? "";
      const nextIsLyric =
        next.trim() !== "" && !chordLineTokens(next) && !directive(next) && !sectionHeading(next) && !inlineChords(next);
      if (nextIsLyric) {
        parsed.push({
          text: next,
          chords: chordTokens.map((t) => ({ name: t.name, col: Math.min(t.col, next.length) })),
          section: pendingSection,
        });
        i++;
      } else {
        parsed.push({ text: "", chords: chordTokens.map((t, k) => ({ name: t.name, col: k })), section: pendingSection });
      }
      pendingSection = undefined;
      continue;
    }
    // plain lyric line without chords
    parsed.push({ text: line, chords: [], section: pendingSection });
    pendingSection = undefined;
  }

  // Guess timing: one bar per chord, two bars for a line with one chord or none.
  const bpb = doc.beatsPerBar;
  let beat = 0;
  const chords: ChordEvent[] = [];
  const lines: LyricLine[] = [];
  for (const p of parsed) {
    const lineStart = beat;
    const barsForLine = p.chords.length <= 1 ? 2 : p.chords.length;
    const lineBeats = barsForLine * bpb;
    if (p.text || p.section) {
      const l: LyricLine = { beat: lineStart, text: p.text };
      if (p.section) l.section = p.section;
      lines.push(l);
    }
    p.chords.forEach((c, k) => {
      const at = p.chords.length === 1 ? lineStart : lineStart + k * bpb;
      const ev: ChordEvent = { beat: at, name: c.name };
      if (p.text) ev.col = c.col;
      // don't repeat a chord that is already sounding (e.g. a carried chord written again)
      if (chords.length && chords[chords.length - 1].name === c.name && chords[chords.length - 1].beat === at) return;
      chords.push(ev);
    });
    beat += lineBeats;
  }
  doc.chords = chords;
  doc.lines = lines;
  doc.length = beat;
  if (!parsed.length) warnings.push("No chords or lyrics found.");
  return { doc, warnings };
}

/** Writes a song as ChordPro text (chords inline in the lyrics). */
export function exportChordPro(doc: SongDoc, chordName: (s: string) => string = (s) => s): string {
  const out: string[] = [];
  out.push(`{title: ${doc.title}}`);
  if (doc.artist) out.push(`{artist: ${doc.artist}}`);
  if (doc.key) out.push(`{key: ${doc.key}}`);
  if (doc.bpm) out.push(`{tempo: ${Math.round(doc.bpm)}}`);
  out.push(`{time: ${doc.beatsPerBar}/4}`);
  if (doc.capo) out.push(`{capo: ${doc.capo}}`);
  out.push("");
  // chords before the first line (intro)
  const firstLineBeat = doc.lines[0]?.beat ?? Infinity;
  const intro = doc.chords.filter((c) => c.beat < firstLineBeat);
  if (intro.length) {
    out.push("{comment: Intro}");
    out.push(intro.map((c) => `[${chordName(c.name)}]`).join(" "));
    out.push("");
  }
  doc.lines.forEach((line, i) => {
    if (line.section) {
      if (i > 0) out.push("");
      out.push(`{comment: ${line.section}}`);
    }
    const placed = chordsInLine(doc, i).filter((p) => !p.carried);
    let text = line.text;
    // insert from the right so earlier columns stay valid
    for (const p of [...placed].sort((a, b) => b.col - a.col || b.index - a.index)) {
      text = text.slice(0, p.col) + `[${chordName(p.chord.name)}]` + text.slice(p.col);
    }
    out.push(text);
  });
  return out.join("\n") + "\n";
}

/** Chords-over-lyrics rows for one line: the chord row and the lyric row, aligned. */
export function chordOverLyricRows(
  text: string,
  placed: { name: string; col: number }[],
): { chordRow: { name: string; col: number }[]; lyric: string } {
  // Shift chords right when two would overlap (one space minimum between names).
  const row: { name: string; col: number }[] = [];
  let minCol = 0;
  for (const p of [...placed].sort((a, b) => a.col - b.col)) {
    const col = Math.max(p.col, minCol);
    row.push({ name: p.name, col });
    minCol = col + p.name.length + 1;
  }
  // pad the lyric so chords past its end still line up
  const width = row.length ? Math.max(text.length, row[row.length - 1].col) : text.length;
  return { chordRow: row, lyric: text.padEnd(width, " ") };
}
