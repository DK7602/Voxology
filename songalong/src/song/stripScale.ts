// How wide a beat is in the chord strip. Every beat gets the same width, so the strip moves at
// the song's pace everywhere (a bar of quick changes used to be wider than a bar of one chord,
// and the strip raced through it); the width is set per song so its quick chords still have
// room for their names.

export interface StripSize {
  /** pixels per beat */
  beat: number;
  /** room a chord name needs */
  name: number;
}

/** The strip's scale for a song whose chord cells last `beats` each (in beats). */
export function stripSize(beats: number[], o: { lead: boolean; narrow: boolean }): StripSize {
  const lens = beats.filter((b) => b > 0).sort((a, b) => a - b);
  // the quick chords, though one passing chord in ten may squeeze its name
  const short = lens.length ? lens[Math.floor(lens.length * 0.1)] : 4;
  // on a phone, at most 44 px a beat: two bars ahead stay in view (names squeeze instead)
  const name = o.lead ? 44 : o.narrow ? 44 : 64;
  const [lo, hi] = o.lead ? [40, 140] : o.narrow ? [28, 44] : [36, 110];
  return { beat: Math.min(hi, Math.max(lo, name / short)), name };
}
