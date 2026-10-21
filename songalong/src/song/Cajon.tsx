// The cajon: the song's cajon part (shared/music/cajon.ts) as a strip of strokes that scrolls
// with the song, the front of a cajon showing where to hit, and a card for each groove.

import { useMemo, useRef } from "react";
import type { SongRecord } from "../../shared/api";
import { cajonPart, hands, SUBS, type CajonLevel, type CajonPart, type Groove, type Stroke } from "../../shared/music/cajon";
import type { SongDoc } from "../../shared/types";
import { IconChevronLeft, IconChevronRight, IconSpeaker } from "../components/Icons";
import type { PlayerEngine } from "../player/engine";
import { useBeatSelect, usePlayerState, useTick } from "../player/hooks";
import { cajonHit, unlockAudio } from "../player/synth";
import { useNarrow } from "./ChordStrip";
import { PLAYHEAD } from "./LyricsLane";

type Hit = Exclude<Stroke, ".">;
const NAMES: Record<Hit, string> = { B: "Bass", S: "Slap", t: "tap" };

export function useCajonPart(doc: SongDoc, level: CajonLevel): CajonPart {
  return useMemo(
    () => cajonPart({ beatsPerBar: doc.beatsPerBar, bpm: doc.bpm, length: doc.length, drums: doc.drums }, level),
    [doc, level],
  );
}

const barCount = (part: CajonPart) => part.barGroove.length;
const grooveAt = (part: CajonPart, bar: number): Groove => part.grooves[part.barGroove[Math.max(0, Math.min(barCount(part) - 1, bar))]];

/** The strokes by bar: a column per beat, its four 16th notes in it; the now line stays put. */
export function GrooveStrip({ engine, part }: { engine: PlayerEngine; part: CajonPart }) {
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
  const onBeat = (beat: number, e: React.MouseEvent) => {
    const bar = Math.floor(beat / bpb);
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
          return (
            <div key={b} className={"bar gs-bar" + (b % 4 === 0 ? " phrase" : "") + (groove.rest ? " rest" : "")} role="listitem" aria-label={`Bar ${b + 1}: ${groove.rest ? "rest" : "groove " + groove.name}`}>
              {groove.rest && <span className="gs-rest">no drums</span>}
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
                    title={`Bar ${b + 1}, beat ${k + 1}: click to jump, shift-click to loop bars`}
                  >
                    <span className="gs-num">{k === 0 ? b + 1 : k + 1}</span>
                    {Array.from({ length: SUBS }, (_, s) => {
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
                          {x === "t" ? "" : x}
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
const zoneOf = (x: Hit, hand: string): Zone => (x === "B" ? "B" : (x === "S" ? "S" : "t") + (hand === "L" ? "L" : "R")) as Zone;

/** The front of the cajon: where each stroke is played, lit like the lead neck. */
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
      <path d="M28 28 h56 v40 q-28 8 -56 0z" className={z("SL")} />
      <text x="56" y="56" className="cz-label">S</text>
      <path d="M126 28 h56 v40 q-28 8 -56 0z" className={z("SR")} />
      <text x="154" y="56" className="cz-label">S</text>
      <circle cx="74" cy="92" r="10" className={z("tL")} />
      <circle cx="136" cy="92" r="10" className={z("tR")} />
      <circle cx="105" cy="168" r="52" className={z("B")} />
      <text x="105" y="176" className="cz-label big">B</text>
      {(["L", "R"] as const).map((h) => (
        <g key={h} className={"cz-hand" + (hand === h ? " on" : "")}>
          <circle cx={h === "L" ? 30 : 180} cy="104" r="11" />
          <text x={h === "L" ? 30 : 180} y="108.5">{h}</text>
        </g>
      ))}
    </svg>
  );
}

/** A groove as a step grid: Slap, Bass and tap rows, a column per 8th note. */
function StepGrid({ groove, level, now = -1, small = false }: { groove: Groove; level: CajonLevel; now?: number; small?: boolean }) {
  const hand = hands(groove.bar, level);
  const cols = groove.bar.length / 2;
  const counts = Array.from({ length: cols }, (_, i) => (i % 2 ? "&" : String(i / 2 + 1)));
  const rows: [Hit, string][] = [["S", "Slap"], ["B", "Bass"], ["t", "Tap"]];
  return (
    <div className={"step-grid" + (small ? " small" : "")} style={{ gridTemplateColumns: `${small ? 18 : 46}px repeat(${cols}, minmax(0, 1fr))` }}>
      {!small && <span />}
      {!small && counts.map((c, i) => <span key={i} className={"sg-count" + (i % 2 ? "" : " beat")}>{c}</span>)}
      {rows.map(([k, name]) => [
        <span key={k} className="sg-row">{small ? k : name}</span>,
        ...counts.map((_, i) => {
          const s = i * 2;
          const on = groove.bar[s] === k;
          return (
            <span key={`${k}${i}`} className={"sg-cell" + (on ? " on " + k : "") + (Math.floor(now / 2) === i ? " col-now" : "")}>
              {on && !small ? hand[s] : ""}
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
export function CajonPanel({ engine, part, status }: { engine: PlayerEngine; part: CajonPart; status: SongRecord["cajon"] }) {
  const slotsPerBar = part.beatsPerBar * SUBS;
  const slot = useBeatSelect(engine, (beat) => Math.floor(Math.max(0, beat) * SUBS + 1e-6));
  const bar = Math.min(barCount(part) - 1, Math.floor(slot / slotsPerBar));
  const col = slot - bar * slotsPerBar;
  const groove = grooveAt(part, bar);
  const next = grooveAt(part, bar + 1);
  const hand = hands(groove.bar, part.level);
  const nextHand = hands(next.bar, part.level);

  const zones: Record<Zone, ZoneState> = { SL: "", SR: "", B: "", tL: "", tR: "" };
  next.bar.forEach((x, s) => x !== "." && (zones[zoneOf(x, nextHand[s])] = "next"));
  groove.bar.forEach((x, s) => x !== "." && (zones[zoneOf(x, hand[s])] = "bar"));
  const nowStroke = groove.bar[col];
  if (nowStroke && nowStroke !== ".") zones[zoneOf(nowStroke, hand[col])] = "now";

  return (
    <div className="cajon-panel inset">
      <div className="lead-head">
        <b>Cajon</b>
        <span>
          Bar {bar + 1} · {groove.rest ? "no drums here: rest" : part.source === "suggested" ? "suggested groove" : `groove ${groove.name}`}
        </span>
        <span className="faint">gold = now, green = this bar, dashed = next bar</span>
      </div>
      <div className="cajon-body">
        <CajonFace zones={zones} hand={nowStroke && nowStroke !== "." ? hand[col] : ""} />
        <div className="cajon-side">
          <StepGrid groove={groove} level={part.level} now={col} />
          <div className="cajon-legend">
            <span><i className="gs-hit B">B</i>Bass: palm in the middle</span>
            <span><i className="gs-hit S">S</i>Slap: fingers at the top edge</span>
            <span><i className="gs-hit t" />Tap: a light touch</span>
          </div>
          <div className="faint cajon-source">{sourceNote(part, status)}</div>
        </div>
      </div>
    </div>
  );
}

/** One card per groove (in order of first use), with the current one raised; the speaker plays a bar. */
export function GrooveCards({ engine, part, bpm }: { engine: PlayerEngine; part: CajonPart; bpm: number }) {
  const bpb = part.beatsPerBar;
  const current = useBeatSelect(engine, (beat) => part.barGroove[Math.max(0, Math.min(barCount(part) - 1, Math.floor(beat / bpb)))]);
  const track = useRef<HTMLDivElement>(null);
  const cards = part.grooves
    .map((g, i) => ({ g, i, bars: part.barGroove.filter((x) => x === i).length }))
    .filter(({ g }) => !g.rest);

  const play = async (g: Groove) => {
    const ctx = await unlockAudio();
    const spb = 60 / (bpm || 100);
    const t0 = ctx.currentTime + 0.05;
    g.bar.forEach((x, s) => x !== "." && cajonHit(x, t0 + (s / SUBS) * spb, x === "t" ? 0.6 : s % SUBS ? 0.8 : 0.95));
  };
  const scrollBy = (dir: number) => track.current?.scrollBy({ left: dir * track.current.clientWidth * 0.8 });

  return (
    <div className="diagrams inset">
      <button className="icon-btn scroll-arrow left" onClick={() => scrollBy(-1)} aria-label="Previous grooves">
        <IconChevronLeft size={20} />
      </button>
      <div className="diagram-track" ref={track}>
        {cards.map(({ g, i, bars }) => (
          <div key={i} className={"diagram-card groove-card" + (i === current ? " current" : "")}>
            <span className="chord-name">{part.source === "suggested" ? "Suggested" : `Groove ${g.name}`}</span>
            <span className="cat-tag">{part.level === "easy" ? "Easy" : "Groove"}</span>
            <StepGrid groove={g} level={part.level} small />
            <div className="notes">{part.source === "suggested" ? "for the song's tempo" : `${bars} bar${bars === 1 ? "" : "s"}`}</div>
            <div className="card-foot">
              <button onClick={() => play(g)} aria-label={`Play groove ${g.name}`}>
                <IconSpeaker size={20} />
              </button>
              <span />
            </div>
          </div>
        ))}
      </div>
      <button className="icon-btn scroll-arrow right" onClick={() => scrollBy(1)} aria-label="More grooves">
        <IconChevronRight size={20} />
      </button>
    </div>
  );
}
