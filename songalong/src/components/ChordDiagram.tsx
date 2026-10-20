// Chord boxes: fretboard diagrams for guitar / ukulele / mandolin / bass and a keyboard for piano.
import type { FretVoicing, PianoVoicing } from "../../shared/music/voicings";
import { noteName, prettyAccidentals } from "../../shared/music/notes";

interface FretProps {
  voicing: FretVoicing;
  strings: number;
  leftHanded?: boolean;
  /** shown under the box, e.g. "capo 2" */
  caption?: string;
  size?: number;
}

const FRETS_SHOWN = 4;

export function FretDiagram({ voicing, strings, leftHanded, caption, size = 150 }: FretProps) {
  const padX = 22;
  const top = 30;
  const w = 150;
  const gridW = w - padX * 2;
  const fretH = 26;
  const base = voicing.baseFret;
  // four frets, or five when a (bass) shape reaches further
  const highest = Math.max(0, ...voicing.frets.filter((f): f is number => f !== null));
  const fretsShown = Math.min(6, Math.max(FRETS_SHOWN, highest - base + 1));
  const gridH = fretH * fretsShown;
  const h = top + gridH + (caption ? 34 : 18);
  const sx = (s: number) => padX + ((leftHanded ? strings - 1 - s : s) * gridW) / (strings - 1);
  const fy = (f: number) => top + (f - base + 0.5) * fretH;
  const isRiff = !!voicing.order;
  const dotR = strings > 4 ? 9.5 : 11;

  return (
    <svg viewBox={`0 0 ${w} ${h}`} width={size} height={(size * h) / w} className="fret-diagram" role="img"
      aria-label={"Frets: " + voicing.frets.map((f) => (f === null ? "x" : f)).join(" ")}>
      {/* frets */}
      {Array.from({ length: fretsShown + 1 }, (_, i) => (
        <line key={"f" + i} x1={padX} x2={padX + gridW} y1={top + i * fretH} y2={top + i * fretH}
          className="grid" strokeWidth={i === 0 && base === 1 ? 5 : 1.4} />
      ))}
      {/* strings */}
      {Array.from({ length: strings }, (_, s) => (
        <line key={"s" + s} x1={sx(s)} x2={sx(s)} y1={top} y2={top + gridH} className="grid" strokeWidth={1.3} />
      ))}
      {base > 1 && (
        <text x={padX - 8} y={top + fretH * 0.5 + 5} textAnchor="end" className="fret-num">
          {base}
        </text>
      )}
      {/* open / muted markers (bass shows single notes: nothing to mute) */}
      {voicing.frets.map((f, s) =>
        f === null ? (
          voicing.labels ? null : (
            <g key={"m" + s} className="mute" transform={`translate(${sx(s)} ${top - 13})`}>
              <path d="M-5 -5 L5 5 M5 -5 L-5 5" strokeWidth={1.8} />
            </g>
          )
        ) : f === 0 ? (
          voicing.labels ? (
            // bass: an open string to play, as a dot above the nut
            <g key={"o" + s}>
              <circle cx={sx(s)} cy={top - 15} r={dotR - 1} className={voicing.extra?.[s] ? "dot extra" : "dot root"} />
              <text x={sx(s)} y={top - 15 + 4.5} textAnchor="middle" className={"dot-label" + (voicing.extra?.[s] ? " extra" : "")}>
                {voicing.labels[s]}
              </text>
            </g>
          ) : (
            <circle key={"o" + s} cx={sx(s)} cy={top - 13} r={5} className="open" strokeWidth={1.6} />
          )
        ) : null,
      )}
      {/* barres (bass shows single notes: no barres) */}
      {!voicing.labels && voicing.barres
        .filter((b) => b.to > b.from)
        .map((b, i) => {
          const x1 = Math.min(sx(b.from), sx(b.to));
          const x2 = Math.max(sx(b.from), sx(b.to));
          const y = fy(b.fret);
          return (
            <g key={"b" + i}>
              <rect x={x1 - dotR} y={y - dotR * 0.8} width={x2 - x1 + dotR * 2} height={dotR * 1.6} rx={dotR * 0.8} className="dot" />
              <text x={x1} y={y + 4.5} textAnchor="middle" className="dot-label">{b.finger}</text>
              <text x={x2} y={y + 4.5} textAnchor="middle" className="dot-label">{b.finger}</text>
            </g>
          );
        })}
      {/* dots */}
      {voicing.frets.map((f, s) => {
        if (f === null || f === 0) {
          if (isRiff && f === 0 && voicing.order?.[s]) {
            return (
              <text key={"r" + s} x={sx(s)} y={top - 22} textAnchor="middle" className="order-open">
                {voicing.order[s]}
              </text>
            );
          }
          return null;
        }
        const inBarre = voicing.barres.some((b) => b.to > b.from && b.fret === f && s >= b.from && s <= b.to);
        if (inBarre && !isRiff && !voicing.labels) return null;
        const label = voicing.labels ? voicing.labels[s] : isRiff ? voicing.order?.[s] : voicing.fingers[s];
        // bass: the note to play stands out; its fifth and octave are optional rings
        const optional = !!voicing.extra?.[s];
        const main = !!voicing.labels && !optional;
        return (
          <g key={"d" + s}>
            <circle cx={sx(s)} cy={fy(f)} r={dotR} className={isRiff ? "dot riff" : main ? "dot root" : optional ? "dot extra" : "dot"} />
            {label ? (
              <text x={sx(s)} y={fy(f) + 4.5} textAnchor="middle" className={"dot-label" + (optional ? " extra" : "")}>{label}</text>
            ) : null}
          </g>
        );
      })}
      {caption && (
        <text x={w / 2} y={top + gridH + 24} textAnchor="middle" className="caption">{caption}</text>
      )}
    </svg>
  );
}

export function PianoDiagram({ voicing, size = 190 }: { voicing: PianoVoicing; size?: number }) {
  const whites: number[] = [];
  for (let m = voicing.from; m <= voicing.to; m++) if (![1, 3, 6, 8, 10].includes(m % 12)) whites.push(m);
  const ww = 14;
  const wh = 70;
  const bw = 9;
  const bh = 44;
  const w = whites.length * ww;
  const xOfWhite = (m: number) => whites.indexOf(m) * ww;
  const right = new Set(voicing.right);
  const keyClass = (m: number) =>
    m === voicing.left ? "key bass" : right.has(m) ? "key on" : "key";
  // each key named as the chord spells it
  const nameOf = (m: number) => prettyAccidentals(voicing.names?.get(m % 12) ?? noteName(m, true));
  const label = (m: number) => (voicing.order ? String(voicing.order.get(m) ?? "") : nameOf(m));
  const blacks: number[] = [];
  for (let m = voicing.from; m <= voicing.to; m++) if ([1, 3, 6, 8, 10].includes(m % 12)) blacks.push(m);
  return (
    <svg viewBox={`-1 -1 ${w + 2} ${wh + 26}`} width={size} height={(size * (wh + 26)) / (w + 2)} className="piano-diagram" role="img"
      aria-label={"Keys: " + [voicing.left, ...voicing.right].filter((m) => m !== null).map((m) => nameOf(m!)).join(" ")}>
      {whites.map((m) => (
        <g key={m}>
          <rect x={xOfWhite(m)} y={0} width={ww} height={wh} rx={2} className={"white " + keyClass(m)} />
          {(right.has(m) || m === voicing.left) && (
            <text x={xOfWhite(m) + ww / 2} y={wh - 8} textAnchor="middle" className="key-label">{label(m)}</text>
          )}
        </g>
      ))}
      {blacks.map((m) => {
        const x = xOfWhite(m - 1) + ww - bw / 2;
        return (
          <g key={m}>
            <rect x={x} y={0} width={bw} height={bh} rx={1.5} className={"black " + keyClass(m)} />
            {(right.has(m) || m === voicing.left) && (
              <text x={x + bw / 2} y={bh + 13} textAnchor="middle" className="key-label small">{label(m)}</text>
            )}
          </g>
        );
      })}
      <text x={0} y={wh + 22} className="hand-label">{voicing.left !== null ? "left hand" : ""}</text>
      <text x={w} y={wh + 22} textAnchor="end" className="hand-label">{voicing.left !== null ? "right hand" : ""}</text>
    </svg>
  );
}
