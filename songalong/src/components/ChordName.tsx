import { CATEGORY_LABEL, displayChord, parseChord, type ChordCategory } from "../../shared/music/chords";

/** A chord symbol set like a chord chart: B♭m⁷, D/F♯. */
export function ChordName({ symbol, className }: { symbol: string; className?: string }) {
  const d = displayChord(symbol);
  const cat = parseChord(symbol)?.category;
  if (cat === "note" || cat === "notes") {
    return (
      <span className={"chord-name is-note " + (className ?? "")} title={cat === "note" ? "Single note" : "Notes"}>
        <span className="note-mark" aria-hidden="true">♪</span>
        {d.root}
      </span>
    );
  }
  return (
    <span className={"chord-name " + (className ?? "")}>
      {d.root}
      {d.quality && <span className="q">{d.quality}</span>}
      {d.ext && <sup>{d.ext}</sup>}
      {d.bass && <span className="bass">{d.bass}</span>}
    </span>
  );
}

export function chordCategory(symbol: string): ChordCategory | "unknown" {
  return parseChord(symbol)?.category ?? "unknown";
}

/** Colored tag naming the chord type (Triad, Power chord, Seventh...). */
export function CategoryTag({ symbol }: { symbol: string }) {
  const c = parseChord(symbol);
  if (!c) return null;
  let label = CATEGORY_LABEL[c.category];
  if (c.category === "triad") label = c.suffix === "m" ? "Minor triad" : c.suffix === "" ? "Major triad" : c.suffix === "dim" ? "Diminished" : "Augmented";
  if (c.bass !== undefined) label += " · slash";
  return <span className={"cat-tag cat-" + c.category}>{label}</span>;
}

export const CATEGORY_ORDER: ChordCategory[] = ["triad", "sus", "added", "seventh", "extended", "power", "note", "notes"];
