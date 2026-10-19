import { useMemo } from "react";
import { leadPositions } from "../../shared/lead";
import { shapeFor } from "../../shared/music/capo";
import { noteMidi, parseChord } from "../../shared/music/chords";
import { noteName, prettyAccidentals } from "../../shared/music/notes";
import { FRETTED } from "../../shared/music/voicings";
import { indexAt } from "../../shared/timeline";
import type { PlayerEngine } from "../player/engine";
import { useBeatSelect, type ChordView, type ViewSettings } from "../player/hooks";

/** Lead mode: the notes of this bar and the next on the neck (or keyboard), the current one lit. */
export function LeadNeck({ engine, view, settings, leftHanded }: { engine: PlayerEngine; view: ChordView; settings: ViewSettings; leftHanded: boolean }) {
  const doc = engine.timeline.doc; // its chords are the lead part here
  const events = doc.chords;
  const bpb = doc.beatsPerBar || 4;
  const pos = useBeatSelect(engine, (b) => `${indexAt(events, b)}:${Math.floor(Math.max(0, b) / bpb)}`);
  const [current, bar] = pos.split(":").map(Number);
  const from = bar * bpb;
  const to = from + 2 * bpb;
  const visible = events.map((e, i) => ({ e, i })).filter(({ e }) => e.beat >= from - 1e-6 && e.beat < to - 1e-6);

  const inst = FRETTED[settings.instrument === "piano" ? "guitar" : settings.instrument];
  const capo = view.usesCapo ? settings.capo : 0;
  const spots = useMemo(() => {
    if (settings.instrument === "piano") return [];
    const shapes = events.map((e) => (capo ? shapeFor(view.sounding(e.name), capo) : view.sounding(e.name)));
    return leadPositions(shapes, events.map((e) => e.beat), inst.id, bpb);
  }, [events, view, capo, inst.id, bpb, settings.instrument]);

  if (settings.instrument === "piano") {
    return <LeadKeys events={visible} current={current} sounding={view.sounding} barEnd={from + bpb} />;
  }

  const strings = inst.tuning.length;
  const frets = 15;
  const W = 1000;
  const H = 40 + strings * 30;
  const nutX = 50;
  const fretX = (f: number) => nutX + (f * (W - nutX - 20)) / frets;
  const noteX = (f: number) => (f === 0 ? nutX - 22 : (fretX(f - 1) + fretX(f)) / 2);
  // highest string on top, like tab
  const sy = (s: number) => 24 + (leftHanded ? s : strings - 1 - s) * 30;
  const markers = [3, 5, 7, 9, 12, 15];

  return (
    <div className="lead-neck inset">
      <div className="lead-head">
        <b>Lead</b>
        <span className="faint">
          Bars {bar + 1}–{bar + 2}
          {capo ? ` · capo ${capo}` : ""} · gold = now, green = this bar, outlined = next bar
        </span>
      </div>
      <svg viewBox={`0 0 ${W} ${H}`} className="neck" role="img" aria-label="Lead notes on the neck">
        <rect x={nutX} y={sy(leftHanded ? 0 : strings - 1) - 12} width={W - nutX - 20} height={(strings - 1) * 30 + 24} className="neck-wood" rx={6} />
        {markers.map((m) => (
          <circle key={m} cx={(fretX(m - 1) + fretX(m)) / 2} cy={24 + ((strings - 1) * 30) / 2} r={m === 12 ? 6 : 5} className="neck-marker" />
        ))}
        {Array.from({ length: frets + 1 }, (_, f) => (
          <line key={f} x1={fretX(f)} x2={fretX(f)} y1={sy(leftHanded ? 0 : strings - 1) - 12} y2={sy(leftHanded ? strings - 1 : 0) + 12} className={f === 0 ? "neck-nut" : "neck-fret"} />
        ))}
        {Array.from({ length: frets }, (_, f) => (
          <text key={"n" + f} x={(fretX(f) + fretX(f + 1)) / 2} y={H - 4} textAnchor="middle" className="neck-num">{f + 1}</text>
        ))}
        {inst.tuning.map((open, s) => (
          <g key={s}>
            <line x1={nutX} x2={W - 20} y1={sy(s)} y2={sy(s)} className="neck-string" strokeWidth={1 + (strings - 1 - s) * 0.35} />
            <text x={14} y={sy(s) + 5} className="neck-num">{noteName(open, false)}</text>
          </g>
        ))}
        {visible.map(({ e, i }, order) => {
          const nextBar = e.beat >= from + bpb - 1e-6;
          return (spots[i] ?? []).map((p, k) => (
            <g key={`${i}-${k}`} className={"neck-note" + (i === current ? " now" : nextBar ? " next" : "")}>
              <circle cx={noteX(p.fret)} cy={sy(p.string)} r={i === current ? 15 : 12} />
              <text x={noteX(p.fret)} y={sy(p.string) + 4.5} textAnchor="middle">{prettyAccidentals(noteName(p.midi, view.keyName.includes("b")))}</text>
              {k === 0 && <text x={noteX(p.fret) + 13} y={sy(p.string) - 13} className="neck-order">{order + 1}</text>}
            </g>
          ));
        })}
      </svg>
      {visible.length === 0 && <div className="faint" style={{ padding: "4px 12px" }}>No lead notes in these bars — rest, or play along with the chords.</div>}
    </div>
  );
}

function LeadKeys({
  events,
  current,
  sounding,
  barEnd,
}: {
  events: { e: { beat: number; name: string }; i: number }[];
  current: number;
  sounding: (s: string) => string;
  barEnd: number;
}) {
  const from = 48;
  const to = 83;
  const lit = new Map<number, "now" | "bar" | "next">();
  const order = new Map<number, number>();
  events.forEach(({ e, i }, n) => {
    const c = parseChord(sounding(e.name));
    if (!c || c.category === "none") return;
    const seq = c.category === "notes" ? c.sequence ?? [] : [c.root];
    seq.forEach((pc, k) => {
      let m = noteMidi(c, k) ?? 60 + pc;
      while (m < from) m += 12;
      while (m > to) m -= 12;
      const state = i === current ? "now" : e.beat >= barEnd - 1e-6 ? "next" : "bar";
      if (lit.get(m) !== "now") lit.set(m, state);
      if (!order.has(m)) order.set(m, n + 1);
    });
  });
  const isBlack = (m: number) => [1, 3, 6, 8, 10].includes(m % 12);
  const whites: number[] = [];
  for (let m = from; m <= to; m++) if (!isBlack(m)) whites.push(m);
  const ww = 24;
  const xw = (m: number) => whites.indexOf(m) * ww;
  return (
    <div className="lead-neck inset">
      <div className="lead-head">
        <b>Lead</b>
        <span className="faint">gold = now, green = this bar, outlined = next bar</span>
      </div>
      <svg viewBox={`-1 -1 ${whites.length * ww + 2} 132`} className="neck" role="img" aria-label="Lead notes on the keyboard">
        {whites.map((m) => (
          <g key={m}>
            <rect x={xw(m)} y={0} width={ww} height={120} rx={3} className={"lk white " + (lit.get(m) ?? "")} />
            {lit.has(m) && <text x={xw(m) + ww / 2} y={110} textAnchor="middle" className="lk-label">{prettyAccidentals(noteName(m, true))}</text>}
            {order.has(m) && <text x={xw(m) + ww / 2} y={95} textAnchor="middle" className="neck-order">{order.get(m)}</text>}
          </g>
        ))}
        {Array.from({ length: to - from + 1 }, (_, k) => from + k)
          .filter(isBlack)
          .map((m) => (
            <rect key={m} x={xw(m - 1) + ww - 8} y={0} width={16} height={74} rx={2} className={"lk black " + (lit.get(m) ?? "")} />
          ))}
      </svg>
    </div>
  );
}
