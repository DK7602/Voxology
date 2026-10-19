// "Find the best capo": for each capo position, how hard are the shapes you'd play?

import { parseChord, transposeSymbol } from "./chords";
import { fretVoicings, type FrettedInstrument } from "./voicings";
import type { ChordEvent } from "../types";
import { chordEnd } from "../timeline";

export interface CapoOption {
  capo: number;
  /** average difficulty of the shapes, weighted by how long each chord lasts */
  difficulty: number;
  /** the shape names you would play, in order of first use */
  shapes: string[];
  /** shapes that need a full barre */
  barres: number;
}

/** Chord shape you play with a capo on `capo` to sound `symbol`. */
export function shapeFor(symbol: string, capo: number): string {
  return capo ? transposeSymbol(symbol, -capo, "common") : symbol;
}

export function capoOptions(
  chords: readonly ChordEvent[],
  length: number,
  instrument: FrettedInstrument["id"] = "guitar",
  maxCapo = 7,
): CapoOption[] {
  const weights = new Map<string, number>();
  chords.forEach((c, i) => {
    const ch = parseChord(c.name);
    if (!ch || ch.category === "none") return;
    weights.set(c.name, (weights.get(c.name) ?? 0) + Math.max(0.5, chordEnd(chords, i, length) - c.beat));
  });
  const total = [...weights.values()].reduce((a, b) => a + b, 0) || 1;
  const out: CapoOption[] = [];
  for (let capo = 0; capo <= maxCapo; capo++) {
    let d = 0;
    let barres = 0;
    const shapes: string[] = [];
    for (const [name, w] of weights) {
      const shape = shapeFor(name, capo);
      const v = fretVoicings(shape, instrument, 1)[0];
      if (!shapes.includes(shape)) shapes.push(shape);
      if (!v) {
        d += 8 * w;
        continue;
      }
      d += v.difficulty * w;
      if (v.barres.some((b) => b.to - b.from >= 3)) barres++;
    }
    // a capo itself is a small cost, and high capos sound thin
    out.push({ capo, difficulty: Math.round((d / total + capo * 0.08) * 100) / 100, shapes, barres });
  }
  return out;
}

export function bestCapo(chords: readonly ChordEvent[], length: number, instrument: FrettedInstrument["id"] = "guitar"): CapoOption {
  return capoOptions(chords, length, instrument).reduce((best, o) => (o.difficulty < best.difficulty ? o : best));
}
