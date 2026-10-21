// The cajon: the song's cajon part (shared/music/cajon.ts) as a strip of strokes that scrolls
// with the song, the front of a cajon showing where to hit, and a card for each groove, which the
// player can fix by tapping its steps.

import { useMemo, useRef, useState } from "react";
import type { SongRecord } from "../../shared/api";
import {
  cajonPart,
  doubleHit,
  hands,
  SUBS,
  tapStroke,
  withFixes,
  type CajonBarFixes,
  type CajonFixes,
  type CajonLevel,
  type CajonPart,
  type Groove,
  type Stroke,
} from "../../shared/music/cajon";
import type { SongDoc } from "../../shared/types";
import { IconChevronLeft, IconChevronRight, IconEdit, IconSpeaker } from "../components/Icons";
import { Modal } from "../components/ui";
import type { PlayerEngine } from "../player/engine";
import { useBeatSelect, usePlayerState, useTick } from "../player/hooks";
import { cajonHit, unlockAudio } from "../player/synth";
import { useNarrow } from "./ChordStrip";
import { PLAYHEAD } from "./LyricsLane";

type Hit = Exclude<Stroke, ".">;
const NAMES: Record<Hit, string> = { B: "Bass", S: "Slap", t: "tap", X: "Bass and Slap together" };

export function useCajonPart(doc: SongDoc, level: CajonLevel, fixes?: CajonFixes, barFixes?: CajonBarFixes): CajonPart {
  const found = useMemo(
    () => cajonPart({ beatsPerBar: doc.beatsPerBar, bpm: doc.bpm, length: doc.length, drums: doc.drums }, level),
    [doc, level],
  );
  return useMemo(() => withFixes(found, fixes, barFixes), [found, fixes, barFixes]);
}

/** What a bar plays, in words: its groove, or that the player changed it. */
function barName(part: CajonPart, groove: Groove): string {
  if (groove.barOf !== undefined) {
    const base = part.grooves[groove.barOf];
    return base.rest ? "your strokes here" : `groove ${base.name}, changed here`;
  }
  if (groove.rest) return "no drums here: rest";
  return part.source === "suggested" ? "suggested groove" : `groove ${groove.name}`;
}

/** Holding a finger (or the mouse button) this long on a beat of the strip opens its bar to fix. */
const LONG_PRESS_MS = 500;

/** Plays one bar of strokes now (the groove cards' speaker). */
async function playBar(bar: readonly Stroke[], bpm: number) {
  const ctx = await unlockAudio();
  const spb = 60 / (bpm || 100);
  const t0 = ctx.currentTime + 0.05;
  bar.forEach((x, s) => {
    const when = t0 + (s / SUBS) * spb;
    for (const one of x === "X" ? (["B", "S"] as const) : x === "." ? [] : [x]) cajonHit(one, when, one === "t" ? 0.6 : s % SUBS ? 0.8 : 0.95);
  });
}

const barCount = (part: CajonPart) => part.barGroove.length;
const grooveAt = (part: CajonPart, bar: number): Groove => part.grooves[part.barGroove[Math.max(0, Math.min(barCount(part) - 1, bar))]];
const restsAt = (part: CajonPart, bar: number) => bar < barCount(part) && part.grooves[part.barGroove[bar]].rest;

/** The first bar after `bar` where the drums play (-1: none to the end). */
function nextDrumBar(part: CajonPart, bar: number): number {
  for (let b = bar + 1; b < barCount(part); b++) if (!restsAt(part, b)) return b;
  return -1;
}

/** The strokes by bar: a column per beat, its two 8th notes in it; the now line stays put. A long
 *  press (or a double click) on a beat opens its bar to fix. */
export function GrooveStrip({ engine, part, onFixBar }: { engine: PlayerEngine; part: CajonPart; onFixBar: (bar: number) => void }) {
  const bpb = part.beatsPerBar;
  const narrow = useNarrow();
  const beatW = narrow ? 48 : 64;
  const { loop } = usePlayerState(engine);
  const scroller = useRef<HTMLDivElement>(null);
  const beatEls = useRef<(HTMLDivElement | null)[]>([]);
  const hitEls = useRef(new Map<number, HTMLSpanElement>());
  const lit = useRef({ beat: -1, slot: -1 });

  // Every beat is as wide as the next, so where "now" is follows from the beat alone; what is
  // lit changes by class, without re-rendering hundreds of strokes every frame.
  useTick(engine, (_t, beat) => {
    const sc = scroller.current;
    if (!sc) return;
    sc.scrollLeft = Math.max(0, beat) * beatW;
    const b = Math.floor(beat + 1e-6);
    const slot = Math.floor(beat * SUBS + 1e-6);
    const was = lit.current;
    if (b !== was.beat) {
      beatEls.current[was.beat]?.classList.remove("now");
      beatEls.current[b]?.classList.add("now");
    }
    if (slot !== was.slot) {
      hitEls.current.get(was.slot)?.classList.remove("now");
      hitEls.current.get(slot)?.classList.add("now");
    }
    lit.current = { beat: b, slot };
  });

  const loopFrom = loop.on ? loop.from ?? 0 : null;
  const loopTo = loop.on ? loop.to ?? Infinity : null;
  const anchor = useRef<number | null>(null);
  // a press held still on a beat; `fired` once it opened the bar (the click that ends it then does nothing)
  const press = useRef<{ timer: number; x: number; y: number; fired: boolean } | null>(null);
  const startPress = (beat: number, e: React.PointerEvent) => {
    if (e.button > 0) return;
    const bar = Math.floor(beat / bpb);
    const p = { x: e.clientX, y: e.clientY, fired: false, timer: 0 };
    p.timer = window.setTimeout(() => {
      p.fired = true;
      onFixBar(bar);
    }, LONG_PRESS_MS);
    press.current = p;
  };
  const movePress = (e: React.PointerEvent) => {
    const p = press.current;
    if (p && !p.fired && Math.hypot(e.clientX - p.x, e.clientY - p.y) > 10) {
      window.clearTimeout(p.timer);
      press.current = null;
    }
  };
  const endPress = () => {
    const p = press.current;
    if (!p) return;
    window.clearTimeout(p.timer);
    if (!p.fired) press.current = null;
  };
  // the bar of a first click: a click jumps there and the strip moves, so a double click's second
  // click lands on another bar
  const clicked = useRef(-1);
  const onBeat = (beat: number, e: React.MouseEvent) => {
    if (press.current?.fired) {
      press.current = null;
      return;
    }
    if (e.detail > 1) return;
    const bar = Math.floor(beat / bpb);
    clicked.current = bar;
    if (e.shiftKey || e.metaKey || e.ctrlKey) {
      // a range of bars to loop
      const from = anchor.current ?? Math.floor(engine.beat / bpb);
      engine.setLoop({ on: true, from: Math.min(from, bar) * bpb, to: (Math.max(from, bar) + 1) * bpb });
      anchor.current = from;
      return;
    }
    anchor.current = bar;
    engine.seekBeat(beat);
  };

  hitEls.current.clear();
  lit.current = { beat: -1, slot: -1 };
  return (
    <div className="strip-wrap inset groove-strip">
      <div className="strip" ref={scroller} role="list" aria-label="Cajon strokes by bar">
        <div className="strip-pad" style={{ width: `${PLAYHEAD * 100}%` }} aria-hidden="true" />
        {part.barGroove.map((g, b) => {
          const groove = part.grooves[g];
          // the last bar of a rest says the drums come in next
          const cue = groove.rest && b + 1 < barCount(part) && !restsAt(part, b + 1);
          const own = groove.barOf !== undefined;
          return (
            <div
              key={b}
              className={"bar gs-bar" + (b % 4 === 0 ? " phrase" : "") + (groove.rest ? " rest" : "") + (own ? " own" : "")}
              role="listitem"
              aria-label={`Bar ${b + 1}: ${cue ? "rest, drums next bar" : barName(part, groove)}`}
            >
              {groove.rest && <span className={"gs-rest" + (cue ? " cue" : "")}>{cue ? "Next Bar" : "no drums"}</span>}
              {Array.from({ length: bpb }, (_, k) => {
                const beat = b * bpb + k;
                const inLoop = loopFrom !== null && loopTo !== null && beat >= loopFrom - 1e-6 && beat + 1 <= loopTo + 1e-6;
                return (
                  <div
                    key={k}
                    ref={(el) => {
                      beatEls.current[beat] = el;
                    }}
                    className={"gs-beat" + (inLoop ? " in-loop" : "")}
                    style={{ width: beatW }}
                    onClick={(e) => onBeat(beat, e)}
                    onDoubleClick={() => onFixBar(clicked.current >= 0 ? clicked.current : b)}
                    onPointerDown={(e) => startPress(beat, e)}
                    onPointerMove={movePress}
                    onPointerUp={endPress}
                    onPointerCancel={endPress}
                    onPointerLeave={endPress}
                    onContextMenu={(e) => e.preventDefault()}
                    title={`Bar ${b + 1}, beat ${k + 1}: click to jump, shift-click to loop bars, hold or double-click to fix the bar`}
                  >
                    <span className="gs-num">{k === 0 ? b + 1 : k + 1}</span>
                    {/* the parts use 8th notes only: the beat and its "and", room for big dots */}
                    {[0, SUBS / 2].map((s) => {
                      const x = groove.bar[k * SUBS + s];
                      if (x === ".") return <span key={s} />;
                      const slot = beat * SUBS + s;
                      return (
                        <span
                          key={s}
                          ref={(el) => {
                            if (el) hitEls.current.set(slot, el);
                          }}
                          className={"gs-hit " + x}
                          aria-label={NAMES[x]}
                        >
                          {x === "t" ? "" : x === "X" ? "BS" : x}
                        </span>
                      );
                    })}
                  </div>
                );
              })}
            </div>
          );
        })}
        <div className="strip-pad" style={{ width: `${(1 - PLAYHEAD) * 100}%` }} aria-hidden="true" />
      </div>
      <div className="strip-playhead" style={{ left: `calc(4px + ${PLAYHEAD} * (100% - 8px))` }} aria-hidden="true" />
    </div>
  );
}

type Zone = "SL" | "SR" | "B" | "tL" | "tR";
type ZoneState = "now" | "bar" | "next" | "";
/** Where a stroke is played (a double hit: the bass, and the slap of the hand whose turn it is). */
const zonesOf = (x: Hit, hand: string): Zone[] =>
  x === "X" ? ["B", `S${doubleHit(hand as "R" | "L" | "").S}` as Zone] : [(x === "B" ? "B" : (x === "S" ? "S" : "t") + (hand === "L" ? "L" : "R")) as Zone];
/** The hands that play a stroke ("RL" for a double hit). */
const handsOf = (x: Stroke | undefined, hand: string) => (x === "X" ? "RL" : x && x !== "." ? hand : "");

/** The front of the cajon: where each stroke is played, lit like the lead neck (`hand`: the hands
 *  playing now, "R", "L" or "RL"). It is drawn as the
 *  player sees it sitting on the cajon and looking down at the front: their right hand plays the
 *  side on the left of the picture, the left hand the side on the right. */
function CajonFace({ zones, hand }: { zones: Record<Zone, ZoneState>; hand: string }) {
  const z = (k: Zone) => "cz " + zones[k];
  return (
    <svg className="cajon-face" viewBox="0 0 210 272" role="img" aria-label="The front of the cajon: where to hit">
      <defs>
        <linearGradient id="cajon-wood" x1="0" y1="0" x2="1" y2="1">
          <stop offset="0" stopColor="#e3b679" />
          <stop offset="0.5" stopColor="#c98f52" />
          <stop offset="1" stopColor="#a86d36" />
        </linearGradient>
      </defs>
      <rect x="15" y="8" width="180" height="256" rx="12" fill="url(#cajon-wood)" stroke="#5a3415" strokeWidth="2.5" />
      {[30, 55, 80, 105, 130, 155, 180, 205, 230].map((y) => (
        <path key={y} d={`M24 ${y} q90 ${6 - (y % 3) * 4} 162 0`} stroke="rgba(90,50,20,0.13)" fill="none" />
      ))}
      <rect x="15" y="8" width="180" height="16" rx="8" fill="#7d4c22" />
      {[[26, 32], [184, 32], [26, 252], [184, 252]].map(([x, y]) => (
        <circle key={`${x}${y}`} cx={x} cy={y} r="3" fill="#4a2a10" />
      ))}
      <path d="M28 28 h56 v40 q-28 8 -56 0z" className={z("SR")} />
      <text x="56" y="56" className="cz-label">S</text>
      <path d="M126 28 h56 v40 q-28 8 -56 0z" className={z("SL")} />
      <text x="154" y="56" className="cz-label">S</text>
      <circle cx="74" cy="92" r="10" className={z("tR")} />
      <circle cx="136" cy="92" r="10" className={z("tL")} />
      <circle cx="105" cy="168" r="52" className={z("B")} />
      <text x="105" y="176" className="cz-label big">B</text>
      {(["L", "R"] as const).map((h) => (
        <g key={h} className={"cz-hand" + (hand.includes(h) ? " on" : "")}>
          <circle cx={h === "R" ? 30 : 180} cy="104" r="11" />
          <text x={h === "R" ? 30 : 180} y="108.5">{h}</text>
        </g>
      ))}
    </svg>
  );
}

/** A groove as a step grid: Slap, Bass and tap rows, a column per 8th note. With `onTap`, each
 *  step is a button (the stroke there, or the row's stroke put there). */
function StepGrid({
  groove,
  level,
  now = -1,
  small = false,
  onTap,
}: {
  groove: Groove;
  level: CajonLevel;
  now?: number;
  small?: boolean;
  onTap?: (slot: number, stroke: Hit) => void;
}) {
  const hand = hands(groove.bar, level);
  const cols = groove.bar.length / 2;
  const counts = Array.from({ length: cols }, (_, i) => (i % 2 ? "&" : String(i / 2 + 1)));
  const rows: [Hit, string][] = [["S", "Slap"], ["B", "Bass"], ["t", "Tap"]];
  return (
    <div
      className={"step-grid" + (small ? " small" : "") + (onTap ? " editing" : "")}
      style={{ gridTemplateColumns: `${small ? 18 : 46}px repeat(${cols}, minmax(0, 1fr))` }}
    >
      {!small && <span />}
      {!small && counts.map((c, i) => <span key={i} className={"sg-count" + (i % 2 ? "" : " beat")}>{c}</span>)}
      {rows.map(([k, name]) => [
        <span key={k} className="sg-row">{small ? k : name}</span>,
        ...counts.map((c, i) => {
          const s = i * 2;
          const x = groove.bar[s];
          const on = x === k || (x === "X" && k !== "t");
          const className = "sg-cell" + (on ? " on " + k : "") + (Math.floor(now / 2) === i ? " col-now" : "");
          // a double hit: the hand whose turn it is slaps, the other plays the bass
          const label = !on || small ? "" : x === "X" ? doubleHit(hand[s])[k as "B" | "S"] : hand[s];
          return onTap ? (
            <button key={`${k}${i}`} className={className} aria-pressed={on} aria-label={`${name} on ${c === "&" ? `the and of ${i / 2 + 0.5}` : c}`} onClick={() => onTap(s, k)}>
              {label}
            </button>
          ) : (
            <span key={`${k}${i}`} className={className}>
              {label}
            </span>
          );
        }),
      ])}
    </div>
  );
}

function sourceNote(part: CajonPart, status: SongRecord["cajon"]): string {
  if (part.source === "drums") return "From the song's drums: kick → Bass, snare → Slap, hi-hat → light taps.";
  if (status === "queued" || status === "working" || status === "waiting") {
    return "This song's own cajon part is being worked out from its drums and will show up here. Until then, a suggested groove for its tempo.";
  }
  if (status === "ready") return "No drums were heard in this recording, so here's a suggested groove for its tempo.";
  return "A suggested groove for the song's tempo (its drums aren't known).";
}

/** Where to hit now, this bar and the next, on the cajon's front and as a step grid. */
export function CajonPanel({
  engine,
  part,
  status,
  onFixBar,
}: {
  engine: PlayerEngine;
  part: CajonPart;
  status: SongRecord["cajon"];
  onFixBar: (bar: number) => void;
}) {
  const slotsPerBar = part.beatsPerBar * SUBS;
  const slot = useBeatSelect(engine, (beat) => Math.floor(Math.max(0, beat) * SUBS + 1e-6));
  const bar = Math.min(barCount(part) - 1, Math.floor(slot / slotsPerBar));
  const col = slot - bar * slotsPerBar;
  const groove = grooveAt(part, bar);
  const next = grooveAt(part, bar + 1);
  const comeIn = groove.rest ? nextDrumBar(part, bar) : -1;
  const hand = hands(groove.bar, part.level);
  const nextHand = hands(next.bar, part.level);

  const zones: Record<Zone, ZoneState> = { SL: "", SR: "", B: "", tL: "", tR: "" };
  const light = (x: Stroke | undefined, h: string, state: ZoneState) => x && x !== "." && zonesOf(x, h).forEach((z) => (zones[z] = state));
  next.bar.forEach((x, s) => light(x, nextHand[s], "next"));
  groove.bar.forEach((x, s) => light(x, hand[s], "bar"));
  const nowStroke = groove.bar[col];
  light(nowStroke, hand[col], "now");
  const doubles = part.grooves.some((g) => g.bar.includes("X"));

  return (
    <div className="cajon-panel inset">
      <div className="lead-head">
        <b>Cajon</b>
        <span>
          Bar {bar + 1} · {barName(part, groove)}
        </span>
        {comeIn >= 0 && <span className={"cajon-next" + (comeIn === bar + 1 ? " soon" : "")}>Next beat on Bar {comeIn + 1}</span>}
        <button className="btn btn-sm fix-bar" onClick={() => onFixBar(bar)}>
          <IconEdit size={16} /> Fix this bar
        </button>
        <span className="faint">gold = now, green = this bar, dashed = next bar</span>
      </div>
      <div className="cajon-body">
        <CajonFace zones={zones} hand={handsOf(nowStroke, hand[col])} />
        <div className="cajon-side">
          <StepGrid groove={groove} level={part.level} now={col} />
          <div className="cajon-legend">
            <span><i className="gs-hit B">B</i>Bass: palm in the middle</span>
            <span><i className="gs-hit S">S</i>Slap: fingers at the top edge</span>
            <span><i className="gs-hit t" />Tap: a light touch</span>
            {doubles && <span><i className="gs-hit X">BS</i>Bass and Slap at once, both hands</span>}
          </div>
          <div className="faint cajon-source">{sourceNote(part, status)}</div>
        </div>
      </div>
    </div>
  );
}

/** One card per groove (in order of first use), with the current one raised; the speaker plays a
 *  bar, the pencil opens it for fixing. */
export function GrooveCards({
  engine,
  part,
  bpm,
  onFix,
}: {
  engine: PlayerEngine;
  part: CajonPart;
  bpm: number;
  onFix: (found: readonly Stroke[], bar: readonly Stroke[]) => void;
}) {
  const bpb = part.beatsPerBar;
  // the card of the groove playing now (a bar the player changed raises its groove's card)
  const current = useBeatSelect(engine, (beat) => {
    const g = part.barGroove[Math.max(0, Math.min(barCount(part) - 1, Math.floor(beat / bpb)))];
    return part.grooves[g]?.barOf ?? g;
  });
  const track = useRef<HTMLDivElement>(null);
  const [fixing, setFixing] = useState<number | null>(null);
  const cards = part.grooves
    .map((g, i) => ({ g, i, bars: part.barGroove.filter((x) => x === i || part.grooves[x].barOf === i).length }))
    .filter(({ g }) => !g.rest && g.barOf === undefined);
  const name = (g: Groove) => (part.source === "suggested" ? "Suggested" : `Groove ${g.name}`);
  const scrollBy = (dir: number) => track.current?.scrollBy({ left: dir * track.current.clientWidth * 0.8 });

  return (
    <div className="diagrams inset">
      <button className="icon-btn scroll-arrow left" onClick={() => scrollBy(-1)} aria-label="Previous grooves">
        <IconChevronLeft size={20} />
      </button>
      <div className="diagram-track" ref={track}>
        {cards.map(({ g, i, bars }) => (
          <div key={i} className={"diagram-card groove-card" + (i === current ? " current" : "")}>
            <span className="chord-name">{name(g)}</span>
            <span className="cat-tag">{part.level === "easy" ? "Easy" : "Groove"}</span>
            <StepGrid groove={g} level={part.level} small />
            <div className="notes">
              {part.source === "suggested" ? "for the song's tempo" : `${bars} bar${bars === 1 ? "" : "s"}`}
              {g.found && " · your fix"}
            </div>
            <div className="card-foot">
              <button onClick={() => playBar(g.bar, bpm)} aria-label={`Play ${name(g)}`}>
                <IconSpeaker size={20} />
              </button>
              <button onClick={() => setFixing(i)} aria-label={`Fix ${name(g)}`}>
                <IconEdit size={18} /> Fix
              </button>
            </div>
          </div>
        ))}
      </div>
      <button className="icon-btn scroll-arrow right" onClick={() => scrollBy(1)} aria-label="More grooves">
        <IconChevronRight size={20} />
      </button>
      {fixing !== null && part.grooves[fixing] && (
        <GrooveFixer
          groove={part.grooves[fixing]}
          title={part.source === "suggested" ? "Fix the suggested groove" : `Fix groove ${part.grooves[fixing].name}`}
          level={part.level}
          bpm={bpm}
          onFix={onFix}
          onClose={() => setFixing(null)}
        />
      )}
    </div>
  );
}

/** A groove to fix: tap a step to put the row's stroke there, or a stroke to take it away. The
 *  fix goes for every bar that plays the groove, and is kept for this player and song. */
function GrooveFixer({
  groove,
  title,
  level,
  bpm,
  onFix,
  onClose,
}: {
  groove: Groove;
  title: string;
  level: CajonLevel;
  bpm: number;
  onFix: (found: readonly Stroke[], bar: readonly Stroke[]) => void;
  onClose: () => void;
}) {
  const found = groove.found ?? groove.bar;
  const tap = (s: number, k: Hit) => {
    const bar = [...groove.bar];
    bar[s] = tapStroke(bar[s], k as "B" | "S" | "t");
    onFix(found, bar);
  };
  return (
    <Modal title={title} onClose={onClose}>
      <p className="muted" style={{ marginTop: 0 }}>
        Tap a square to play that stroke there; tap a colored one to take it away. Bass and Slap in the same column make a double hit,
        both hands at once. Every bar of this groove changes, and your fix is kept for you on this song.
      </p>
      <StepGrid groove={groove} level={level} onTap={tap} />
      <div className="fixer-foot">
        <button className="btn" onClick={() => playBar(groove.bar, bpm)}>
          <IconSpeaker size={18} /> Play
        </button>
        {groove.found && (
          <button className="btn" onClick={() => onFix(found, found)}>
            Back to the groove as found
          </button>
        )}
        <button className="btn btn-primary" onClick={onClose}>
          Done
        </button>
      </div>
    </Modal>
  );
}

/**
 * One bar to fix (from the strip or the cajon panel; the song waits there): the same steps as a
 * groove, changing just this bar unless the player picks every bar of its groove. The arrows go
 * to the bars either side.
 */
export function BarFixer({
  part,
  bar,
  bpm,
  onFixGroove,
  onFixBar,
  onBar,
  onClose,
}: {
  part: CajonPart;
  bar: number;
  bpm: number;
  onFixGroove: (found: readonly Stroke[], bar: readonly Stroke[]) => void;
  onFixBar: (bar: number, found: readonly Stroke[], base: readonly Stroke[], next: readonly Stroke[]) => void;
  onBar: (bar: number) => void;
  onClose: () => void;
}) {
  const [scope, setScope] = useState<"bar" | "groove">("bar");
  const here = part.grooves[part.barGroove[bar]];
  const base = part.grooves[here.barOf ?? part.barGroove[bar]];
  const found = base.found ?? base.bar;
  const whole = scope === "groove" && !base.rest;
  const shown = whole ? base : here;
  const name = part.source === "suggested" ? "the suggested groove" : `groove ${base.name}`;
  const tap = (s: number, k: Hit) => {
    const next = [...shown.bar];
    next[s] = tapStroke(next[s], k as "B" | "S" | "t");
    if (whole) onFixGroove(found, next);
    else onFixBar(bar, found, base.bar, next);
  };
  return (
    <Modal title={`Fix bar ${bar + 1}`} onClose={onClose}>
      {!base.rest && (
        <div className="seg fixer-scope" role="group" aria-label="What to change">
          <button aria-pressed={!whole} onClick={() => setScope("bar")}>
            Just bar {bar + 1}
          </button>
          <button aria-pressed={whole} onClick={() => setScope("groove")}>
            Every bar of {name.replace(/^the /, "")}
          </button>
        </div>
      )}
      <p className="muted">
        {base.rest ? "No drums play in this bar; strokes you add are for this bar only. " : ""}
        Tap a square to play that stroke there; tap a colored one to take it away. Bass and Slap in the same column make a double hit.{" "}
        {whole
          ? `Every bar of ${name} changes${here.barOf !== undefined ? ` (bar ${bar + 1} keeps its own change)` : ""}.`
          : `Only bar ${bar + 1} changes.`}
      </p>
      <StepGrid groove={shown} level={part.level} onTap={tap} />
      <div className="fixer-foot">
        <button className="btn" onClick={() => onBar(bar - 1)} disabled={bar === 0} aria-label="The bar before">
          <IconChevronLeft size={18} /> Bar {bar}
        </button>
        <button className="btn" onClick={() => onBar(bar + 1)} disabled={bar + 1 >= barCount(part)} aria-label="The bar after">
          Bar {bar + 2} <IconChevronRight size={18} />
        </button>
        <button className="btn" onClick={() => playBar(shown.bar, bpm)}>
          <IconSpeaker size={18} /> Play
        </button>
        {!whole && here.barOf !== undefined && (
          <button className="btn" onClick={() => onFixBar(bar, found, base.bar, base.bar)}>
            {base.rest ? "Back to no drums" : `Back to ${name}`}
          </button>
        )}
        {whole && base.found && (
          <button className="btn" onClick={() => onFixGroove(found, found)}>
            Back to the groove as found
          </button>
        )}
        <button className="btn btn-primary" onClick={onClose}>
          Done
        </button>
      </div>
    </Modal>
  );
}
