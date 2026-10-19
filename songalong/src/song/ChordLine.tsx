import { ChordName } from "../components/ChordName";

export interface LinePlacement {
  /** chord symbol to show */
  name: string;
  /** character index in the text */
  col: number;
  /** the chord sounding right now */
  now?: boolean;
  key: string | number;
}

/** A lyric line with each chord sitting above the word it falls on. */
export function ChordLine({ text, chords }: { text: string; chords: LinePlacement[] }) {
  const sorted = [...chords].sort((a, b) => a.col - b.col);
  if (!text.trim()) {
    return (
      <span className="cl-line instrumental">
        {sorted.map((c) => (
          <span className="cl-seg" key={c.key}>
            <span className={"cl-chord" + (c.now ? " now" : "")}>
              <ChordName symbol={c.name} />
            </span>
            <span className="cl-words"> </span>
          </span>
        ))}
      </span>
    );
  }
  // group chords that share a column
  const groups: { col: number; items: LinePlacement[] }[] = [];
  for (const c of sorted) {
    const col = Math.max(0, Math.min(c.col, text.length));
    const g = groups.at(-1);
    if (g && g.col === col) g.items.push(c);
    else groups.push({ col, items: [c] });
  }
  const segs: { chord: LinePlacement[] | null; words: string; key: string }[] = [];
  if (!groups.length || groups[0].col > 0) segs.push({ chord: null, words: text.slice(0, groups[0]?.col ?? text.length), key: "start" });
  groups.forEach((g, i) => {
    const end = i + 1 < groups.length ? groups[i + 1].col : text.length;
    segs.push({ chord: g.items, words: text.slice(g.col, end), key: "g" + g.col });
  });
  return (
    <span className="cl-line">
      {segs.map((s) => (
        <span className="cl-seg" key={s.key}>
          <span className={"cl-chord" + (s.chord?.some((c) => c.now) ? " now" : "")}>
            {s.chord?.map((c, i) => (
              <span key={c.key}>
                {i > 0 && " "}
                <ChordName symbol={c.name} />
              </span>
            ))}
          </span>
          <span className="cl-words">{s.words || " "}</span>
        </span>
      ))}
    </span>
  );
}
