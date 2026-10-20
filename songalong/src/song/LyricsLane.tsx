import { useLayoutEffect, useMemo, useRef, type ReactNode } from "react";
import { indexAt, wordStarts } from "../../shared/timeline";
import type { PlayerEngine } from "../player/engine";
import { useBeatSelect, usePlayerState, useTick } from "../player/hooks";
import { ribbonRows, singingPace, untimedEnd } from "./ribbon";

/** Where "now" sits, as a share of the width. The chord strip uses the same spot. */
export const PLAYHEAD = 0.22;

/** The line's words, each in a span marked with its word number (data-wi), so the lane can find
 *  where every word is. */
function markedWords(text: string): ReactNode[] {
  const out: ReactNode[] = [];
  let pos = 0;
  wordStarts(text).forEach((at, k) => {
    if (at > pos) out.push(text.slice(pos, at));
    let end = at;
    while (end < text.length && !/\s/.test(text[end])) end++;
    out.push(
      <span key={k} data-wi={k}>
        {text.slice(at, end)}
      </span>,
    );
    pos = end;
  });
  if (pos < text.length) out.push(text.slice(pos));
  return out;
}

interface Anchor {
  b: number;
  x: number;
}

/** x at beat b, straight-line between the anchors around it. */
function xAt(pts: Anchor[], b: number, pxPerBeat: number): number {
  if (!pts.length) return 0;
  if (b <= pts[0].b) return pts[0].x + (b - pts[0].b) * pxPerBeat;
  const last = pts[pts.length - 1];
  if (b >= last.b) return last.x;
  let lo = 0;
  let hi = pts.length - 1;
  while (hi - lo > 1) {
    const mid = (lo + hi) >> 1;
    if (pts[mid].b <= b) lo = mid;
    else hi = mid;
  }
  const p = pts[lo];
  const q = pts[hi];
  return q.b > p.b ? p.x + ((b - p.b) / (q.b - p.b)) * (q.x - p.x) : q.x;
}

/** The lyrics on one line, scrolling past a fixed "now" line: word by word where the song has
 *  word times, otherwise steadily through each line. */
export function LyricsLane({ engine }: { engine: PlayerEngine }) {
  const doc = engine.timeline.doc;
  const length = engine.timeline.lengthBeats;
  const { countIn } = usePlayerState(engine);
  const rows = useMemo(() => ribbonRows(doc, length), [doc, length]);
  const pace = useMemo(() => singingPace(rows), [rows]);
  const rowStarts = useMemo(() => rows.map((r) => ({ beat: r.start })), [rows]);
  // instrumental parts: about 95 px a second
  const pxPerBeat = Math.min(70, Math.max(26, (95 * 60) / (doc.bpm || 100)));

  const rowIdx = useBeatSelect(engine, (beat) => indexAt(rowStarts, beat));

  const scroller = useRef<HTMLDivElement>(null);
  const track = useRef<HTMLDivElement>(null);
  const rowRefs = useRef<(HTMLDivElement | null)[]>([]);
  const anchors = useRef<Anchor[]>([]);

  const place = (beat: number) => {
    const sc = scroller.current;
    if (!sc) return;
    sc.scrollLeft = xAt(anchors.current, beat, pxPerBeat) - PLAYHEAD * sc.clientWidth;
  };

  useLayoutEffect(() => {
    const measure = () => {
      const tr = track.current;
      if (!tr) return;
      const base = tr.getBoundingClientRect().left;
      const pts: Anchor[] = [];
      rows.forEach((row, r) => {
        const el = rowRefs.current[r];
        if (!el) return;
        const spans = [...el.querySelectorAll<HTMLElement>("[data-wi]")].map((s) => s.getBoundingClientRect());
        const content = (el.querySelector(".rb-words, .rb-rest") ?? el).getBoundingClientRect();
        const right = (spans.at(-1)?.right ?? content.right) - base;
        let lastB: number;
        if (row.words && spans.length === row.words.length) {
          // each word reaches "now" as it is sung
          row.words.forEach((b, k) => pts.push({ b, x: spans[k].left - base }));
          lastB = row.words[row.words.length - 1];
          const endB = Math.min(row.wordsEnd ?? lastB + 1, row.end - 0.1);
          // the last word is crossed while it lasts
          if (endB > lastB) pts.push({ b: endB, x: right });
          return;
        }
        pts.push({ b: row.start, x: content.left - base });
        lastB = row.start;
        // where the words are when the chords change
        const starts = wordStarts(row.text);
        for (const m of row.marks) {
          if (m.beat <= lastB || !spans.length) continue;
          let k = 0;
          while (k + 1 < starts.length && starts[k + 1] <= m.col) k++;
          const s = spans[k];
          const len = Math.max(1, row.text.slice(starts[k]).search(/\s|$/));
          const x = m.col >= row.text.length ? right : s.left + Math.min(1, Math.max(0, (m.col - starts[k]) / len)) * s.width - base;
          pts.push({ b: m.beat, x });
          lastB = m.beat;
        }
        // reach the end of the words, then wait there and cross the gap to the next row
        const endB = untimedEnd(row, lastB, pace, doc.bpm);
        if (endB > lastB) pts.push({ b: endB, x: right });
      });
      pts.sort((p, q) => p.b - q.b);
      for (let k = 1; k < pts.length; k++) pts[k].x = Math.max(pts[k].x, pts[k - 1].x);
      anchors.current = pts;
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
    // re-measure when the rows change
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [rows, pace, pxPerBeat]);

  useTick(engine, (_t, beat) => place(beat));

  return (
    <div className="lane inset" aria-live="off">
      <div className="ribbon" ref={scroller}>
        <div className="ribbon-track" ref={track}>
          {rows.map((row, r) => {
            const when = r < rowIdx ? " past" : r === rowIdx ? " current" : "";
            const sung = row.text.trim();
            return (
              <div
                key={r}
                ref={(el) => {
                  rowRefs.current[r] = el;
                }}
                className={"rb-row" + when + (sung ? "" : " instrumental")}
                onClick={() => engine.seekBeat(row.start)}
              >
                {row.section && <span className="rb-section">{row.section}</span>}
                {sung ? (
                  <span className="rb-words">{markedWords(row.text)}</span>
                ) : (
                  // music without words: a rest as long as it lasts
                  <span className="rb-rest" style={{ width: Math.min(1600, Math.max(56, (row.end - row.start) * pxPerBeat)) }}>
                    ♪
                  </span>
                )}
              </div>
            );
          })}
        </div>
      </div>
      <div className="lane-playhead" style={{ left: `${PLAYHEAD * 100}%` }} aria-hidden="true" />
      {countIn !== null && (
        <div className="countdown" aria-live="assertive">
          {countIn}
        </div>
      )}
    </div>
  );
}
