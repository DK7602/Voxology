import { useEffect, useRef } from "react";
import { chordsInLine, indexAt } from "../../shared/timeline";
import { CATEGORY_LABEL } from "../../shared/music/chords";
import type { PlayerEngine } from "../player/engine";
import { useBeatSelect, type ChordView } from "../player/hooks";
import { ChordLine } from "./ChordLine";
import { CATEGORY_ORDER } from "../components/ChordName";

/** The whole song as a chord sheet; the current line is highlighted and kept in view. */
export function SummaryView({ engine, view }: { engine: PlayerEngine; view: ChordView }) {
  const doc = engine.timeline.doc;
  const pos = useBeatSelect(engine, (b) => `${indexAt(doc.lines, b)}:${indexAt(doc.chords, b)}`);
  const [lineIdx, chordIdx] = pos.split(":").map(Number);
  const box = useRef<HTMLDivElement>(null);
  const lineRefs = useRef<(HTMLElement | null)[]>([]);

  useEffect(() => {
    const el = lineRefs.current[lineIdx];
    const b = box.current;
    if (!el || !b) return;
    const top = el.offsetTop - b.offsetTop - b.clientHeight * 0.3;
    b.scrollTo({ top: Math.max(0, top) });
  }, [lineIdx]);

  const firstLine = doc.lines[0]?.beat ?? Infinity;
  const intro = doc.chords.map((c, i) => ({ c, i })).filter(({ c }) => c.beat < firstLine);

  return (
    <div className="summary inset" ref={box}>
      <div className="legend" style={{ marginBottom: 8 }}>
        {CATEGORY_ORDER.map((c) => (
          <span key={c} className={"cat-" + c}>
            <i /> {CATEGORY_LABEL[c]}
          </span>
        ))}
      </div>
      {intro.length > 0 && (
        <>
          <div className="sheet-section">Intro</div>
          <span className={"sheet-line" + (lineIdx < 0 ? " current" : "")} onClick={() => engine.seekBeat(0)}>
            <ChordLine text="" chords={intro.map(({ c, i }) => ({ name: view.shown(c.name), col: i, key: i, now: i === chordIdx }))} />
          </span>
        </>
      )}
      {doc.lines.map((line, i) => (
        <div key={i}>
          {line.section && <div className="sheet-section">{line.section}</div>}
          <span
            ref={(el) => {
              lineRefs.current[i] = el;
            }}
            className={"sheet-line" + (i === lineIdx ? " current" : "")}
            onClick={() => engine.seekBeat(line.beat)}
          >
            <ChordLine
              text={line.text}
              chords={chordsInLine(doc, i)
                .filter((p) => !p.carried || !line.text)
                .map((p) => ({ name: view.shown(p.chord.name), col: p.col, key: p.index, now: p.index === chordIdx }))}
            />
          </span>
        </div>
      ))}
    </div>
  );
}
