import { useLayoutEffect, useMemo, useRef } from "react";
import { bars as makeBars } from "../../shared/timeline";
import { parseChord } from "../../shared/music/chords";
import { ChordName } from "../components/ChordName";
import type { PlayerEngine } from "../player/engine";
import { useBeatSelect, usePlayerState, useTick, type ChordView } from "../player/hooks";
import { PLAYHEAD } from "./LyricsLane";

interface Cell {
  start: number;
  end: number;
  chordIndex: number;
  carried: boolean; // chord continues from the previous bar
  bar: number;
  phrase: boolean;
}

/** The chord grid: one tile per bar, split where the chord changes inside the bar. */
export function ChordStrip({ engine, view, lead = false }: { engine: PlayerEngine; view: ChordView; lead?: boolean }) {
  const doc = engine.timeline.doc;
  const length = engine.timeline.lengthBeats;
  const { loop } = usePlayerState(engine);
  const cells = useMemo(() => {
    const out: Cell[][] = [];
    const bpb = doc.beatsPerBar || 4;
    for (const bar of makeBars(doc, length)) {
      const row: Cell[] = [];
      bar.changes.forEach((ch, k) => {
        const start = bar.start + ch.at;
        const nextAt = k + 1 < bar.changes.length ? bar.start + bar.changes[k + 1].at : bar.start + bpb;
        row.push({
          start,
          end: nextAt,
          chordIndex: ch.chordIndex,
          carried: ch.at === 0 && Math.abs(doc.chords[ch.chordIndex].beat - bar.start) > 1e-6,
          bar: bar.index,
          phrase: bar.index % 4 === 0,
        });
      });
      if (!row.length) row.push({ start: bar.start, end: bar.start + bpb, chordIndex: -1, carried: false, bar: bar.index, phrase: bar.index % 4 === 0 });
      out.push(row);
    }
    return out;
  }, [doc, length]);
  const flat = useMemo(() => cells.flat(), [cells]);

  const active = useBeatSelect(engine, (beat) => {
    let lo = 0;
    let hi = flat.length - 1;
    let ans = -1;
    while (lo <= hi) {
      const mid = (lo + hi) >> 1;
      if (flat[mid].start <= beat + 1e-6) {
        ans = mid;
        lo = mid + 1;
      } else hi = mid - 1;
    }
    return beat < 0 ? -1 : ans;
  });

  const scroller = useRef<HTMLDivElement>(null);
  const progress = useRef<HTMLDivElement>(null);
  const cellRefs = useRef<(HTMLDivElement | null)[]>([]);
  // each cell's left edge and width inside the scrolling content
  const boxes = useRef<{ x: number; w: number }[]>([]);

  // The strip glides so that "now" stays under the same spot as the lyrics' now line.
  const place = (beat: number) => {
    const sc = scroller.current;
    const bar = progress.current;
    if (!sc || !bar || !flat.length) return;
    let i = 0;
    let lo = 0;
    let hi = flat.length - 1;
    while (lo <= hi) {
      const mid = (lo + hi) >> 1;
      if (flat[mid].start <= beat + 1e-6) {
        i = mid;
        lo = mid + 1;
      } else hi = mid - 1;
    }
    const c = flat[i];
    const box = boxes.current[i];
    if (!box) return;
    const f = (beat - c.start) / (c.end - c.start);
    const x = box.x + Math.min(1, f) * box.w;
    sc.scrollLeft = x - PLAYHEAD * sc.clientWidth;
    bar.style.left = box.x + "px";
    bar.style.width = (beat < c.start ? 0 : Math.min(1, f) * box.w) + "px";
  };

  useLayoutEffect(() => {
    const measure = () => {
      const sc = scroller.current;
      if (!sc) return;
      const base = sc.getBoundingClientRect().left - sc.scrollLeft;
      boxes.current = flat.map((_c, i) => {
        const el = cellRefs.current[i];
        if (!el) return { x: 0, w: 0 };
        const r = el.getBoundingClientRect();
        return { x: r.left - base, w: r.width };
      });
      place(engine.beat);
    };
    measure();
    const ro = new ResizeObserver(measure);
    if (scroller.current) ro.observe(scroller.current);
    let live = true;
    void document.fonts?.ready.then(() => live && measure());
    return () => {
      live = false;
      ro.disconnect();
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [flat, view, lead]);

  useTick(engine, (_t, beat) => place(beat));

  const loopFrom = loop.on ? loop.from ?? 0 : null;
  const loopTo = loop.on ? loop.to ?? length : null;
  const selectStart = useRef<number | null>(null);

  const onCellClick = (c: Cell, e: React.MouseEvent) => {
    const bpb = doc.beatsPerBar || 4;
    if (e.shiftKey || e.metaKey || e.ctrlKey) {
      // select a range of bars to loop
      const anchorBar = selectStart.current ?? Math.floor(engine.beat / bpb);
      const a = Math.min(anchorBar, c.bar);
      const b = Math.max(anchorBar, c.bar);
      engine.setLoop({ on: true, from: a * bpb, to: (b + 1) * bpb });
      selectStart.current = anchorBar;
      return;
    }
    selectStart.current = c.bar;
    engine.seekBeat(c.start);
  };

  let flatIndex = 0;
  return (
    <div className="strip-wrap inset">
      <div className={"strip" + (lead ? " lead" : "")} ref={scroller} role="list" aria-label={lead ? "Lead notes by bar" : "Chords by bar"}>
        <div className="strip-pad" style={{ width: `${PLAYHEAD * 100}%` }} aria-hidden="true" />
        {cells.map((row, b) => (
          <div key={b} className={"bar" + (row[0].phrase ? " phrase" : "")} style={{ minWidth: row.length > 1 ? (lead ? 46 : 70) * row.length + 10 : undefined }}>
            {row.map((c) => {
              const i = flatIndex++;
              const sym = c.chordIndex >= 0 ? doc.chords[c.chordIndex].name : "N.C.";
              const shown = view.shown(sym);
              const cat = parseChord(view.sounding(sym))?.category ?? "none";
              const inLoop = loopFrom !== null && loopTo !== null && c.start >= loopFrom - 1e-6 && c.end <= loopTo + 1e-6;
              return (
                <div
                  key={i}
                  ref={(el) => {
                    cellRefs.current[i] = el;
                  }}
                  role="listitem"
                  className={"cell" + (i === active ? " active" : "") + (c.carried ? " carried" : "") + (inLoop ? " in-loop" : "")}
                  style={{ flexGrow: c.end - c.start }}
                  onClick={(e) => onCellClick(c, e)}
                  title={`Bar ${c.bar + 1}${c.carried ? " (held)" : ""} — click to jump, shift-click to loop bars`}
                >
                  {cat !== "none" && <span className={"cat-dot cat-" + cat} aria-hidden="true" />}
                  {/* no chord (N.C.): a blank card */}
                  {cat === "none" ? <span className="sr-only">No chord</span> : <ChordName symbol={shown} />}
                </div>
              );
            })}
          </div>
        ))}
        <div className="strip-pad" style={{ width: `${(1 - PLAYHEAD) * 100}%` }} aria-hidden="true" />
        <div ref={progress} className="progress" aria-hidden="true" />
      </div>
      <div className="strip-playhead" style={{ left: `calc(4px + ${PLAYHEAD} * (100% - 8px))` }} aria-hidden="true" />
    </div>
  );
}
