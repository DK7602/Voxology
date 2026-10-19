import { useRef } from "react";
import { chordsInLine, indexAt, lineEnd } from "../../shared/timeline";
import type { PlayerEngine } from "../player/engine";
import { useBeatSelect, usePlayerState, useTick, type ChordView } from "../player/hooks";
import { ChordLine, type LinePlacement } from "./ChordLine";

/** Previous, current and the next two lyric lines, with the chords over the words. */
export function LyricsLane({ engine, view }: { engine: PlayerEngine; view: ChordView }) {
  const doc = engine.timeline.doc;
  const { countIn } = usePlayerState(engine);
  const pos = useBeatSelect(engine, (beat) => `${indexAt(doc.lines, beat)}:${indexAt(doc.chords, beat)}`);
  const [lineIdx, chordIdx] = pos.split(":").map(Number);
  const playhead = useRef<HTMLDivElement>(null);
  const currentSlot = useRef<HTMLDivElement>(null);
  const length = engine.timeline.lengthBeats;

  useTick(engine, (_t, beat) => {
    const ph = playhead.current;
    const slot = currentSlot.current;
    if (!ph || !slot) return;
    let start: number;
    let end: number;
    if (lineIdx < 0) {
      start = 0;
      end = doc.lines[0]?.beat ?? length;
    } else {
      start = doc.lines[lineIdx].beat;
      end = lineEnd(doc.lines, lineIdx, length);
    }
    const f = Math.max(0, Math.min(1, (beat - start) / Math.max(1e-6, end - start)));
    const words = slot.querySelector(".cl-line") as HTMLElement | null;
    const box = words ?? slot;
    const x = box.offsetLeft + f * box.offsetWidth;
    ph.style.transform = `translateX(${x}px)`;
  });

  const slotFor = (i: number, role: "past" | "current" | "next" | "far") => {
    const isCurrent = role === "current";
    if (i < 0) {
      // before the first lyric: the intro
      const introChords = doc.chords.filter((c) => c.beat < (doc.lines[0]?.beat ?? Infinity)).slice(0, 6);
      if (!introChords.length && !isCurrent) return <div key="intro" className={"lane-slot " + role} />;
      return (
        <div key="intro" className={"lane-slot " + role} ref={isCurrent ? currentSlot : undefined}>
          {isCurrent && <div ref={playhead} className="playhead" aria-hidden="true" />}
          <span className="section-name">Intro</span>
          {introChords.length ? (
            <ChordLine
              text=""
              chords={introChords.map((c, k) => ({ name: view.shown(c.name), col: k, key: k, now: doc.chords.indexOf(c) === chordIdx }))}
            />
          ) : (
            <span className="lane-empty">♪</span>
          )}
        </div>
      );
    }
    if (i >= doc.lines.length) return <div key={"end" + i} className={"lane-slot " + role} />;
    const line = doc.lines[i];
    const placed: LinePlacement[] = chordsInLine(doc, i).map((p) => ({
      name: view.shown(p.chord.name),
      col: p.col,
      key: p.index,
      now: p.index === chordIdx && isCurrent,
    }));
    return (
      <div key={i} className={"lane-slot " + role} ref={isCurrent ? currentSlot : undefined}>
        {isCurrent && <div ref={playhead} className="playhead" aria-hidden="true" />}
        {line.section && <span className="section-name">{line.section}</span>}
        <ChordLine text={line.text} chords={placed} />
      </div>
    );
  };

  const cur = lineIdx;
  return (
    <div className="lane inset" aria-live="off">
      {cur >= 0 ? slotFor(cur - 1, "past") : <div className="lane-slot past" />}
      {slotFor(cur, "current")}
      {slotFor(cur + 1, "next")}
      {slotFor(cur + 2, "far")}
      {countIn !== null && (
        <div className="countdown" aria-live="assertive">
          {countIn}
        </div>
      )}
    </div>
  );
}
