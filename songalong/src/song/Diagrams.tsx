import { useEffect, useMemo, useRef } from "react";
import { chordNoteNames, parseChord } from "../../shared/music/chords";
import { indexAt } from "../../shared/timeline";
import { CategoryTag, ChordName } from "../components/ChordName";
import { FretDiagram, PianoDiagram } from "../components/ChordDiagram";
import { IconChevronLeft, IconChevronRight, IconDots, IconSpeaker } from "../components/Icons";
import { prettyAccidentals } from "../../shared/music/notes";
import type { PlayerEngine } from "../player/engine";
import { useBeatSelect, type ChordView, type ViewSettings } from "../player/hooks";
import { arpeggio, strum, unlockAudio } from "../player/synth";
import { voicingFor } from "./voicing";

interface Props {
  engine: PlayerEngine;
  view: ChordView;
  settings: ViewSettings;
  leftHanded: boolean;
  choices: Record<string, number>;
  onChoose: (symbol: string, index: number) => void;
}

/** One card per chord in the song (in order of first use): name, type, how to play it. */
export function Diagrams({ engine, view, settings, leftHanded, choices, onChoose }: Props) {
  const doc = engine.timeline.doc;
  const unique = useMemo(() => {
    const seen = new Map<string, string>(); // shown symbol -> original symbol
    for (const c of doc.chords) {
      const p = parseChord(c.name);
      if (!p || p.category === "none") continue;
      const shown = view.shown(c.name);
      if (!seen.has(shown)) seen.set(shown, c.name);
    }
    return [...seen.entries()].map(([shown, original]) => ({ shown, original }));
  }, [doc.chords, view]);

  const currentShown = useBeatSelect(engine, (beat) => {
    const i = indexAt(doc.chords, beat);
    return i >= 0 ? view.shown(doc.chords[i].name) : "";
  });

  const track = useRef<HTMLDivElement>(null);
  const cardRefs = useRef(new Map<string, HTMLDivElement>());
  useEffect(() => {
    const el = cardRefs.current.get(currentShown);
    const tr = track.current;
    if (!el || !tr) return;
    const left = el.offsetLeft - tr.offsetLeft;
    if (left < tr.scrollLeft || left + el.offsetWidth > tr.scrollLeft + tr.clientWidth) {
      tr.scrollTo({ left: Math.max(0, left - 8) });
    }
  }, [currentShown]);

  const scrollBy = (dir: number) => track.current?.scrollBy({ left: dir * track.current.clientWidth * 0.8 });

  const play = async (original: string) => {
    const ctx = await unlockAudio();
    const v = voicingFor(original, view, settings, choices[original] ?? 0);
    if (!v) return;
    if (v.riff || settings.instrument === "bass") arpeggio(v.midis, ctx.currentTime + 0.02, 0.32, settings.instrument);
    else strum(v.midis, ctx.currentTime + 0.02, { instrument: settings.instrument, velocity: 0.9, spread: 0.035 });
  };

  if (!unique.length) return <div className="empty">No chords in this song yet.</div>;

  return (
    <div className="diagrams inset">
      <button className="icon-btn scroll-arrow left" onClick={() => scrollBy(-1)} aria-label="Previous chords">
        <IconChevronLeft size={20} />
      </button>
      <div className="diagram-track" ref={track}>
        {unique.map(({ shown, original }) => {
          const choice = choices[original] ?? 0;
          const v = voicingFor(original, view, settings, choice);
          // the notes of the chord named on the card (the shape, with a capo)
          const chord = parseChord(shown);
          const notes = chord ? chordNoteNames(chord).map(prettyAccidentals).join(" ") : "";
          return (
            <div
              key={shown}
              ref={(el) => {
                if (el) cardRefs.current.set(shown, el);
                else cardRefs.current.delete(shown);
              }}
              className={"diagram-card" + (v?.kind === "piano" ? " piano" : "") + (shown === currentShown ? " current" : "")}
            >
              <ChordName symbol={shown} />
              <CategoryTag symbol={view.sounding(original)} />
              {v?.kind === "fret" && (
                <FretDiagram
                  voicing={v.voicing}
                  strings={v.strings}
                  leftHanded={leftHanded}
                  caption={v.capo ? `capo ${v.capo}` : undefined}
                  size={v.strings > 4 ? 140 : 120}
                />
              )}
              {v?.kind === "piano" && <PianoDiagram voicing={v.voicing} size={210} />}
              {!v && <div className="faint" style={{ padding: 30 }}>No shape found</div>}
              <div className="notes" title="Notes in this chord">{notes}</div>
              <div className="card-foot">
                <button onClick={() => play(original)} aria-label={`Play ${shown}`}>
                  <IconSpeaker size={20} />
                </button>
                {v && v.count > 1 ? (
                  <button
                    onClick={() => onChoose(original, (choice + 1) % v.count)}
                    aria-label={`Another way to play ${shown}`}
                    title="Another way to play it"
                  >
                    {choice + 1}/{v.count} <IconDots size={18} />
                  </button>
                ) : (
                  <span />
                )}
              </div>
            </div>
          );
        })}
      </div>
      <button className="icon-btn scroll-arrow right" onClick={() => scrollBy(1)} aria-label="More chords">
        <IconChevronRight size={20} />
      </button>
    </div>
  );
}
