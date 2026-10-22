// The chord style of a song: as recorded, full chords (a rock song on an acoustic), or power
// chords (an acoustic song on a distorted electric). See shared/music/chordStyle.ts.

import { CHORD_STYLES, type ChordStyle } from "../../shared/music/chordStyle";
import { ChordName } from "../components/ChordName";
import { Modal } from "../components/ui";
import type { ChordView } from "../player/hooks";

export const STYLE_NAMES: Record<ChordStyle, string> = { recorded: "As recorded", full: "Full chords", power: "Power chords" };

const STYLE_NOTES: Record<ChordStyle, string> = {
  recorded: "The chords as the recording plays them.",
  full: "Power chords played as full major or minor chords: for a rock song on an acoustic guitar.",
  power: "Every chord played as its root and fifth: for an acoustic song on a distorted electric.",
};

export function ChordStylePicker({
  style,
  view,
  onStyle,
  onClose,
}: {
  style: ChordStyle;
  view: ChordView;
  onStyle: (style: ChordStyle) => void;
  onClose: () => void;
}) {
  const filled = [...view.full.chords];
  return (
    <Modal title="Chord style" onClose={onClose}>
      <div className="style-choices">
        {CHORD_STYLES.map((s) => (
          <button key={s} className={"btn style-choice" + (style === s ? " active" : "")} aria-pressed={style === s} onClick={() => onStyle(s)}>
            <b>{STYLE_NAMES[s]}</b>
            <span>{STYLE_NOTES[s]}</span>
          </button>
        ))}
      </div>
      {style === "full" &&
        (filled.length ? (
          <div className="style-filled">
            <p className="muted">
              A power chord leaves out whether it is major or minor. Each one is filled in from the melody sung or played over it, or, when
              the melody doesn’t say, from the song’s key:
            </p>
            <ul>
              {filled.map(([power, full]) => (
                <li key={power}>
                  <ChordName symbol={view.recorded(power)} /> → <ChordName symbol={view.recorded(full)} />
                  <span className="faint"> {view.full.by.get(power) === "melody" ? "by the melody" : "by the key"}</span>
                </li>
              ))}
            </ul>
          </div>
        ) : (
          <p className="muted">This song has no power chords: its chords are already full chords.</p>
        ))}
    </Modal>
  );
}
