// The lead part as text, and where to play it on the neck.
//
// Text format (one or more lines; "|" bar lines are optional and only for reading):
//   bar 5: E4 G4 A4:2 | B4:0.5 A4:0.5 G4 r | [E5]:4 | E4-G4-A4:2
//     E4        a note with its octave (middle C = C4); "E" without an octave is fine too
//     :2        how many beats it lasts (default 1)
//     r or -    a rest
//     [E5]      a chord (power chord, double stop...) in the lead part
//     E4-G4-A4  a quick run of notes inside one slot
//     bar 5:    jump to the start of bar 5 (bars count from 1)
//     # ...     a comment, to the end of the line (a "#" inside a note, as in F#4, is a sharp)

import { isChordSymbol, noteMidi, parseChord } from "./music/chords";
import { parseNote } from "./music/notes";
import { FRETTED, type FrettedInstrument } from "./music/voicings";
import type { ChordEvent } from "./types";

const NOTE = /^([A-Ga-g](?:##|bb|#|b|♯|♭)?)(-?\d)?$/;

export function importLead(text: string, beatsPerBar: number): { events: ChordEvent[]; warnings: string[] } {
  const events: ChordEvent[] = [];
  const warnings: string[] = [];
  let beat = 0;
  for (const raw of text.split("\n")) {
    const line = raw.replace(/(^|\s)#.*$/, "").trim();
    if (!line) continue;
    const tokens = line.split(/\s+/);
    for (let k = 0; k < tokens.length; k++) {
      let tok = tokens[k];
      if (tok === "|" || tok === "||") continue;
      if (/^bar$/i.test(tok) && tokens[k + 1]) {
        const n = parseInt(tokens[k + 1], 10);
        if (n >= 1) beat = (n - 1) * beatsPerBar;
        k++;
        continue;
      }
      const barInline = /^bar(\d+):?$/i.exec(tok);
      if (barInline) {
        beat = (parseInt(barInline[1], 10) - 1) * beatsPerBar;
        continue;
      }
      tok = tok.replace(/\|/g, "");
      if (!tok) continue;
      const m = /^(.*?)(?::(\d+(?:\.\d+)?|\d*\.\d+))?$/.exec(tok)!;
      const body = m[1];
      const dur = m[2] ? parseFloat(m[2]) : 1;
      const name = leadName(body);
      if (!name) {
        warnings.push(`Didn't understand "${tok}" in the lead part.`);
        continue;
      }
      events.push({ beat, name });
      beat += dur;
    }
  }
  // a final rest marks where the last note ends
  if (events.length && events.at(-1)!.name !== "N.C.") events.push({ beat, name: "N.C." });
  return { events: mergeRests(events), warnings };
}

function leadName(body: string): string | null {
  if (/^(r|rest|-|x)$/i.test(body)) return "N.C.";
  const bracket = /^\[(.+)\]$/.exec(body);
  if (bracket) return isChordSymbol(bracket[1]) ? bracket[1] : null;
  if (body.includes("-") && !/^-/.test(body)) {
    const parts = body.split("-");
    if (parts.every((p) => NOTE.test(p) && parseNote(p.replace(/-?\d$/, "")))) return "notes:" + parts.map(normNote).join(" ");
    return null;
  }
  if (NOTE.test(body) && parseNote(body.replace(/-?\d$/, ""))) return "note:" + normNote(body);
  return null;
}

function normNote(t: string): string {
  const m = NOTE.exec(t)!;
  return parseNote(m[1])!.name + (m[2] ?? "");
}

function mergeRests(events: ChordEvent[]): ChordEvent[] {
  return events.filter((e, i) => !(e.name === "N.C." && i > 0 && events[i - 1].name === "N.C."));
}

/** Writes the lead part back as text, one line per 4 bars, each line starting "bar N:". */
export function exportLead(events: readonly ChordEvent[], beatsPerBar: number): string {
  if (!events.length) return "";
  const out: string[] = [];
  let line: string[] = [];
  let lineBar = -1;
  let expected = 0;
  const flush = () => {
    if (line.length) out.push(`bar ${lineBar + 1}: ${line.join(" ")}`);
    line = [];
  };
  events.forEach((e, i) => {
    const next = events[i + 1]?.beat;
    if (next === undefined && e.name === "N.C.") return; // the closing rest is implied
    const bar = Math.floor(e.beat / beatsPerBar + 1e-9);
    if (line.length === 0 || Math.abs(e.beat - expected) > 1e-6 || bar >= lineBar + 4) {
      flush();
      lineBar = bar;
      if (Math.abs(e.beat - lineBar * beatsPerBar) > 1e-6) {
        // starts mid-bar: pad with a rest
        line.push(`r:${round(e.beat - lineBar * beatsPerBar)}`);
      }
    }
    const dur = next !== undefined ? next - e.beat : 1;
    line.push(leadToken(e.name) + (Math.abs(dur - 1) > 1e-6 ? `:${round(dur)}` : ""));
    expected = e.beat + dur;
  });
  flush();
  return out.join("\n");
}

function round(x: number): string {
  return String(Math.round(x * 1000) / 1000);
}

function leadToken(name: string): string {
  if (name === "N.C.") return "r";
  if (name.startsWith("note:")) return name.slice(5);
  if (name.startsWith("notes:")) return name.slice(6).trim().split(/\s+/).join("-");
  return `[${name}]`;
}

// ---- where to play it ----

export interface NeckSpot {
  string: number;
  fret: number;
  midi: number;
}

/**
 * Picks a string and fret for every lead note, keeping the hand in one area as long as it can:
 * for each bar the window of 5 frets that holds the most of its notes, then the nearest spot.
 * Notes without an octave go to the octave nearest the previous note.
 */
export function leadPositions(
  symbols: readonly string[],
  beats: readonly number[],
  instId: FrettedInstrument["id"],
  beatsPerBar: number,
): NeckSpot[][] {
  const inst = FRETTED[instId];
  const lowest = inst.tuning.reduce((a, b) => Math.min(a, b), 99);
  const highest = Math.max(...inst.tuning) + 15;
  let prevMidi = lowest + 12;
  // target MIDI notes per event
  const targets: number[][] = symbols.map((sym) => {
    const c = parseChord(sym);
    if (!c || c.category === "none") return [];
    if (c.category === "note" || c.category === "notes") {
      const seq = c.category === "notes" ? c.sequence ?? [] : [c.root];
      return seq.map((pc, i) => {
        let m = noteMidi(c, i);
        if (m === null) {
          m = prevMidi - 6 + ((pc - (prevMidi - 6)) % 12 + 12) % 12;
        }
        while (m < lowest) m += 12;
        while (m > highest) m -= 12;
        prevMidi = m;
        return m;
      });
    }
    // chords in the lead: root, fifth and octave from the root nearest the previous note
    const root = prevMidi - 6 + ((c.root - (prevMidi - 6)) % 12 + 12) % 12;
    return [root, ...c.intervals.slice(1, 3).map((iv) => root + (iv % 12))].filter((m) => m >= lowest && m <= highest);
  });

  const spotsFor = (m: number) => {
    const out: NeckSpot[] = [];
    inst.tuning.forEach((open, s) => {
      const f = m - open;
      if (f >= 0 && f <= 15) out.push({ string: s, fret: f, midi: m });
    });
    return out;
  };

  const result: NeckSpot[][] = targets.map(() => []);
  // group events by bar
  const byBar = new Map<number, number[]>();
  beats.forEach((b, i) => {
    const bar = Math.floor(b / beatsPerBar + 1e-9);
    if (!byBar.has(bar)) byBar.set(bar, []);
    byBar.get(bar)!.push(i);
  });
  let window = 0;
  for (const [, idxs] of [...byBar.entries()].sort((a, b) => a[0] - b[0])) {
    const notes = idxs.flatMap((i) => targets[i]);
    if (notes.length) {
      let best = window;
      let bestScore = -Infinity;
      for (let w = 0; w <= 12; w++) {
        let fit = 0;
        for (const m of notes) if (spotsFor(m).some((p) => (p.fret === 0 && w <= 2) || (p.fret >= w && p.fret <= w + 4))) fit++;
        const score = fit * 10 - Math.abs(w - window) * 0.6 - w * 0.15;
        if (score > bestScore) {
          bestScore = score;
          best = w;
        }
      }
      window = best;
    }
    let last: NeckSpot | null = null;
    for (const i of idxs) {
      result[i] = targets[i].map((m) => {
        const cands = spotsFor(m);
        if (!cands.length) return null;
        const cost = (p: NeckSpot) => {
          const inWin = (p.fret === 0 && window <= 2) || (p.fret >= window && p.fret <= window + 4);
          return (inWin ? 0 : 10 + Math.abs(p.fret - window)) + (last ? Math.abs(p.string - last.string) * 0.5 : 0);
        };
        const pick = cands.sort((a, b) => cost(a) - cost(b))[0];
        last = pick;
        return pick;
      }).filter((p): p is NeckSpot => p !== null);
    }
  }
  return result;
}
