import { useMemo } from "react";
import type { SongDoc } from "../../shared/types";
import { capoOptions } from "../../shared/music/capo";
import { ChordName } from "../components/ChordName";
import { Modal } from "../components/ui";
import type { ChordView } from "../player/hooks";

/** Compares capo positions by how easy the resulting shapes are. */
export function CapoFinder({
  doc,
  view,
  instrument,
  current,
  onPick,
  onClose,
}: {
  doc: SongDoc;
  view: ChordView;
  instrument: "guitar" | "ukulele" | "mandolin";
  current: number;
  onPick: (capo: number) => void;
  onClose: () => void;
}) {
  const options = useMemo(() => {
    const sounding = doc.chords.map((c) => ({ ...c, name: view.sounding(c.name) }));
    return capoOptions(sounding, doc.length, instrument, 9);
  }, [doc, view, instrument]);
  const best = options.reduce((a, b) => (b.difficulty < a.difficulty ? b : a));
  return (
    <Modal title="Find the best capo" onClose={onClose} wide>
      <p className="muted" style={{ marginTop: 0 }}>
        Each row shows the shapes you would play with the capo there. Lower difficulty = easier (open chords, fewer
        barres). The song still sounds in {view.keyName}.
      </p>
      <table className="capo-table">
        <thead>
          <tr>
            <th>Capo</th>
            <th>Shapes</th>
            <th>Barres</th>
            <th>Difficulty</th>
          </tr>
        </thead>
        <tbody>
          {options.map((o) => (
            <tr key={o.capo} className={o.capo === best.capo ? "best" : ""} onClick={() => onPick(o.capo)}>
              <td>
                {o.capo === 0 ? "None" : o.capo}
                {o.capo === best.capo && " ★"}
                {o.capo === current && " (now)"}
              </td>
              <td style={{ display: "flex", flexWrap: "wrap", gap: "2px 10px" }}>
                {o.shapes.map((s) => (
                  <ChordName key={s} symbol={s} />
                ))}
              </td>
              <td>{o.barres}</td>
              <td>{o.difficulty.toFixed(1)}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </Modal>
  );
}
