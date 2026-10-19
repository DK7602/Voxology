import type { ReactNode } from "react";
import { wordStarts } from "../../shared/timeline";
import { ChordName } from "../components/ChordName";

export interface LinePlacement {
  /** chord symbol to show */
  name: string;
  /** character index in the text */
  col: number;
  /** the chord sounding right now */
  now?: boolean;
  key: string | number;
  /** index in doc.chords, exposed as data-ci so the one-line lyrics can find each chord */
  index?: number;
}

/** The words of `slice` (which starts at `offset` in the line) that start in it, each in a span
 *  marked with its word number, so the one-line lyrics can find where every word is. */
function markWords(slice: string, offset: number, starts: number[]): ReactNode[] {
  const out: ReactNode[] = [];
  let pos = 0;
  starts.forEach((st, k) => {
    const at = st - offset;
    if (at < 0 || at >= slice.length) return;
    if (at > pos) out.push(slice.slice(pos, at));
    let end = at;
    while (end < slice.length && !/\s/.test(slice[end])) end++;
    out.push(
      <span key={k} data-wi={k}>
        {slice.slice(at, end)}
      </span>,
    );
    pos = end;
  });
  if (pos < slice.length) out.push(slice.slice(pos));
  return out;
}

/** A lyric line with each chord sitting above the word it falls on. With `wordMarks`, each word
 *  is marked (data-wi) for the one-line lyrics. */
export function ChordLine({ text, chords, wordMarks = false }: { text: string; chords: LinePlacement[]; wordMarks?: boolean }) {
  const sorted = [...chords].sort((a, b) => a.col - b.col);
  if (!text.trim()) {
    return (
      <span className="cl-line instrumental">
        {sorted.map((c) => (
          <span className="cl-seg" key={c.key}>
            <span className={"cl-chord" + (c.now ? " now" : "")} data-ci={c.index}>
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
  const segs: { chord: LinePlacement[] | null; words: string; at: number; key: string }[] = [];
  if (!groups.length || groups[0].col > 0) segs.push({ chord: null, words: text.slice(0, groups[0]?.col ?? text.length), at: 0, key: "start" });
  groups.forEach((g, i) => {
    const end = i + 1 < groups.length ? groups[i + 1].col : text.length;
    segs.push({ chord: g.items, words: text.slice(g.col, end), at: g.col, key: "g" + g.col });
  });
  const starts = wordMarks ? wordStarts(text) : [];
  return (
    <span className="cl-line">
      {segs.map((s) => (
        <span className="cl-seg" key={s.key}>
          <span className={"cl-chord" + (s.chord?.some((c) => c.now) ? " now" : "")}>
            {s.chord?.map((c, i) => (
              <span key={c.key} data-ci={c.index} className={c.now ? "now" : undefined}>
                {i > 0 && " "}
                <ChordName symbol={c.name} />
              </span>
            ))}
          </span>
          <span className="cl-words">{s.words ? (wordMarks ? markWords(s.words, s.at, starts) : s.words) : " "}</span>
        </span>
      ))}
    </span>
  );
}
