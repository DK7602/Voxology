// Which shape / keys to show and sound for a chord, given the player settings.
import { shapeFor } from "../../shared/music/capo";
import { parseChord } from "../../shared/music/chords";
import {
  FRETTED,
  fretVoicings,
  pianoMidi,
  pianoVoicing,
  voicingMidi,
  type FretVoicing,
  type PianoVoicing,
} from "../../shared/music/voicings";
import type { Instrument } from "../../shared/types";
import type { ChordView, ViewSettings } from "../player/hooks";

export type ShownVoicing =
  | { kind: "fret"; voicing: FretVoicing; count: number; strings: number; capo: number; midis: number[]; riff: boolean }
  | { kind: "piano"; voicing: PianoVoicing; count: 1; midis: number[]; riff: boolean }
  | null;

/** `symbol` is the chord as written in the song. */
export function voicingFor(symbol: string, view: ChordView, s: ViewSettings, choice = 0): ShownVoicing {
  const sounding = view.sounding(symbol);
  const chord = parseChord(sounding);
  if (!chord || chord.category === "none") return null;
  const riff = chord.category === "notes" || chord.category === "note";
  if (s.instrument === "piano") {
    const v = pianoVoicing(sounding);
    if (!v) return null;
    const midis = riff && v.order ? [...v.order.entries()].sort((a, b) => a[1] - b[1]).map(([m]) => m) : pianoMidi(v);
    return { kind: "piano", voicing: v, count: 1, midis, riff };
  }
  const inst = s.instrument as Exclude<Instrument, "piano">;
  const capo = view.usesCapo ? s.capo : 0;
  const shape = capo ? shapeFor(sounding, capo) : sounding;
  const all = fretVoicings(shape, inst, 8);
  if (!all.length) return null;
  const v = all[Math.min(choice, all.length - 1)];
  return {
    kind: "fret",
    voicing: v,
    count: all.length,
    strings: FRETTED[inst].tuning.length,
    capo,
    midis: voicingMidi(v, inst, capo),
    riff,
  };
}
