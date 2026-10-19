// Standard MIDI File export: a chord track (block chords, voiced for piano) and a bass track
// (chord roots / slash basses), at the song's tempo and metre. Sounding pitch: transpose applied.

import { parseChord, transposeSymbol } from "./music/chords";
import { pianoVoicing } from "./music/voicings";
import type { SongDoc } from "./types";
import { chordEnd } from "./timeline";

const PPQ = 480;

function vlq(n: number): number[] {
  const bytes = [n & 0x7f];
  n >>= 7;
  while (n > 0) {
    bytes.unshift((n & 0x7f) | 0x80);
    n >>= 7;
  }
  return bytes;
}

function u32(n: number): number[] {
  return [(n >>> 24) & 255, (n >>> 16) & 255, (n >>> 8) & 255, n & 255];
}

function text(type: number, s: string): number[] {
  const b = [...new TextEncoder().encode(s)];
  return [0xff, type, ...vlq(b.length), ...b];
}

interface Ev {
  tick: number;
  data: number[];
  order: number; // note-offs before note-ons at the same tick
}

function track(events: Ev[], name: string): number[] {
  events.sort((a, b) => a.tick - b.tick || a.order - b.order);
  const body: number[] = [0, ...text(0x03, name)];
  let last = 0;
  for (const e of events) {
    body.push(...vlq(Math.max(0, e.tick - last)), ...e.data);
    last = e.tick;
  }
  body.push(0, 0xff, 0x2f, 0x00);
  return [0x4d, 0x54, 0x72, 0x6b, ...u32(body.length), ...body];
}

export function songToMidi(doc: SongDoc, transpose = 0): Uint8Array<ArrayBuffer> {
  const length = Math.max(doc.length, (doc.chords.at(-1)?.beat ?? 0) + doc.beatsPerBar);
  const tempo = Math.round(60_000_000 / (doc.bpm || 100));
  const markers: Ev[] = doc.lines
    .filter((l) => l.section)
    .map((l) => ({ tick: Math.round(l.beat * PPQ), data: text(0x06, l.section!), order: 0 }));
  const conductorTrack = track(
    [
      { tick: 0, data: [0xff, 0x51, 0x03, (tempo >> 16) & 255, (tempo >> 8) & 255, tempo & 255], order: 0 },
      { tick: 0, data: [0xff, 0x58, 0x04, doc.beatsPerBar, 2, 24, 8], order: 0 },
      ...markers,
    ],
    doc.title || "SongAlong",
  );

  const chordEvents: Ev[] = [];
  const bassEvents: Ev[] = [];
  doc.chords.forEach((c, i) => {
    const sym = transposeSymbol(c.name, transpose, "sharps");
    const chord = parseChord(sym);
    if (!chord || chord.category === "none") return;
    const start = Math.round(Math.max(0, c.beat) * PPQ);
    const end = Math.round(chordEnd(doc.chords, i, length) * PPQ);
    if (end <= start) return;
    const v = pianoVoicing(sym);
    if (!v) return;
    if (chord.category === "notes" && v.order) {
      // riff: spread the notes evenly over the chord's time
      const seq = [...v.order.entries()].sort((a, b) => a[1] - b[1]).map(([m]) => m);
      const step = Math.max(1, Math.floor((end - start) / seq.length));
      seq.forEach((m, k) => {
        const s = start + k * step;
        chordEvents.push({ tick: s, data: [0x90, m, 90], order: 1 }, { tick: s + step - 10, data: [0x80, m, 0], order: 0 });
      });
      return;
    }
    for (const m of v.right) {
      chordEvents.push({ tick: start, data: [0x90, m, 80], order: 1 }, { tick: end - 10, data: [0x80, m, 0], order: 0 });
    }
    if (v.left !== null) {
      const b = v.left - 12;
      bassEvents.push({ tick: start, data: [0x91, b, 95], order: 1 }, { tick: end - 10, data: [0x81, b, 0], order: 0 });
    }
  });

  const chordTrack = track([{ tick: 0, data: [0xc0, 0], order: 0 }, ...chordEvents], "Chords");
  const bassTrack = track([{ tick: 0, data: [0xc1, 33], order: 0 }, ...bassEvents], "Bass");
  const header = [0x4d, 0x54, 0x68, 0x64, ...u32(6), 0, 1, 0, 3, (PPQ >> 8) & 255, PPQ & 255];
  return new Uint8Array([...header, ...conductorTrack, ...chordTrack, ...bassTrack]);
}
