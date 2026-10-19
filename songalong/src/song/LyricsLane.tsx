import { useLayoutEffect, useMemo, useRef } from "react";
import { chordsInLine, gapAfterLine, indexAt } from "../../shared/timeline";
import type { SongDoc } from "../../shared/types";
import { ChordName } from "../components/ChordName";
import type { PlayerEngine } from "../player/engine";
import { useBeatSelect, usePlayerState, useTick, type ChordView } from "../player/hooks";
import { ChordLine } from "./ChordLine";

/** Where "now" sits, as a share of the width. The chord strip uses the same spot. */
export const PLAYHEAD = 0.22;

interface RowChord {
  index: number;
  beat: number;
  col: number;
  carried: boolean;
}

interface Row {
  start: number;
  end: number;
  /** the words; empty for the intro and instrumental breaks */
  text: string;
  section?: string;
  chords: RowChord[];
}

/** The song as one row: the intro, each lyric line, and the instrumental breaks between them. */
export function ribbonRows(doc: SongDoc, length: number): Row[] {
  const rows: Row[] = [];
  const first = doc.lines[0]?.beat ?? length;
  if (first > 0) {
    const chords = doc.chords.flatMap((c, index) => (c.beat < first ? [{ index, beat: c.beat, col: 0, carried: false }] : []));
    rows.push({ start: 0, end: first, text: "", section: doc.lines.length ? "Intro" : undefined, chords });
  }
  doc.lines.forEach((line, i) => {
    const gap = gapAfterLine(doc, i);
    const until = i + 1 < doc.lines.length ? doc.lines[i + 1].beat : Math.max(length, line.beat + 1);
    rows.push({
      start: line.beat,
      end: gap ? gap.start : until,
      text: line.text,
      section: line.section,
      chords: chordsInLine(doc, i).map((p) => ({ index: p.index, beat: p.carried ? line.beat : p.chord.beat, col: p.col, carried: p.carried })),
    });
    if (gap) {
      rows.push({
        start: gap.start,
        end: gap.end,
        text: "",
        chords: gap.chords.map((p) => ({ index: p.index, beat: p.carried ? gap.start : p.chord.beat, col: 0, carried: p.carried })),
      });
    }
  });
  return rows;
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

/** The lyrics on one line with the chords above the words, scrolling past a fixed "now" line. */
export function LyricsLane({ engine, view }: { engine: PlayerEngine; view: ChordView }) {
  const doc = engine.timeline.doc;
  const length = engine.timeline.lengthBeats;
  const { countIn } = usePlayerState(engine);
  const rows = useMemo(() => ribbonRows(doc, length), [doc, length]);
  // instrumental parts: about 95 px a second
  const pxPerBeat = Math.min(70, Math.max(26, (95 * 60) / (doc.bpm || 100)));

  const pos = useBeatSelect(engine, (beat) => {
    let lo = 0;
    let hi = rows.length - 1;
    let r = -1;
    while (lo <= hi) {
      const mid = (lo + hi) >> 1;
      if (rows[mid].start <= beat + 1e-6) {
        r = mid;
        lo = mid + 1;
      } else hi = mid - 1;
    }
    return `${r}:${indexAt(doc.chords, beat)}`;
  });
  const [rowIdx, chordIdx] = pos.split(":").map(Number);

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
        const box = el.getBoundingClientRect();
        pts.push({ b: row.start, x: box.left - base });
        let lastB = row.start;
        for (const c of row.chords) {
          if (c.carried) continue;
          const ce = el.querySelector(`[data-ci="${c.index}"]`);
          if (!ce) continue;
          pts.push({ b: c.beat, x: ce.getBoundingClientRect().left - base });
          lastB = c.beat;
        }
        // reach the end of the words just before the next row starts, then cross the gap
        const words = el.querySelector(".cl-line, .rb-chords") ?? el;
        const endB = row.end - Math.min(0.5, (row.end - lastB) * 0.15);
        if (endB > lastB) pts.push({ b: endB, x: words.getBoundingClientRect().right - base });
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
    // re-measure when the rows or the chord names (transpose, capo) change
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [rows, view, pxPerBeat]);

  useTick(engine, (_t, beat) => place(beat));

  return (
    <div className="lane inset" aria-live="off">
      <div className="ribbon" ref={scroller}>
        <div className="ribbon-track" ref={track}>
          {rows.map((row, r) => {
            const when = r < rowIdx ? " past" : r === rowIdx ? " current" : "";
            return (
              <div
                key={r}
                ref={(el) => {
                  rowRefs.current[r] = el;
                }}
                className={"rb-row" + when + (row.text.trim() ? "" : " instrumental")}
                onClick={() => engine.seekBeat(row.start)}
              >
                {row.section && <span className="rb-section">{row.section}</span>}
                {row.text.trim() ? (
                  <ChordLine
                    text={row.text}
                    chords={row.chords.map((c) => ({
                      name: view.shown(doc.chords[c.index].name),
                      col: c.col,
                      key: c.index,
                      index: c.carried ? undefined : c.index,
                      now: c.index === chordIdx && r === rowIdx,
                    }))}
                  />
                ) : (
                  <InstrumentalRow row={row} doc={doc} view={view} pxPerBeat={pxPerBeat} nowIndex={r === rowIdx ? chordIdx : -1} />
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

/** Chords only, each as wide as it lasts. */
function InstrumentalRow({ row, doc, view, pxPerBeat, nowIndex }: { row: Row; doc: SongDoc; view: ChordView; pxPerBeat: number; nowIndex: number }) {
  const lead = row.chords.length ? row.chords[0].beat - row.start : row.end - row.start;
  return (
    <span className="rb-chords">
      {lead > 0.01 && <span className="rb-rest" style={{ width: Math.min(1600, lead * pxPerBeat) }}>♪</span>}
      {row.chords.map((c, k) => {
        const next = k + 1 < row.chords.length ? row.chords[k + 1].beat : row.end;
        return (
          <span
            key={c.index + (c.carried ? "c" : "")}
            className={"rb-chord" + (c.index === nowIndex ? " now" : "") + (c.carried ? " carried" : "")}
            data-ci={c.carried ? undefined : c.index}
            style={{ width: Math.min(1600, Math.max(56, (next - c.beat) * pxPerBeat)) }}
          >
            <ChordName symbol={view.shown(doc.chords[c.index].name)} />
          </span>
        );
      })}
    </span>
  );
}
