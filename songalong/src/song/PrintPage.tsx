import { useEffect, useMemo, useState } from "react";
import { Link, useParams, useSearchParams } from "react-router-dom";
import type { SongRecord } from "../../shared/api";
import type { Instrument } from "../../shared/types";
import { parseChord } from "../../shared/music/chords";
import { prettyAccidentals } from "../../shared/music/notes";
import { FRETTED } from "../../shared/music/voicings";
import { chordsInLine, gapAfterLine } from "../../shared/timeline";
import { exportLead } from "../../shared/lead";
import { api } from "../api";
import { ChordName, CategoryTag } from "../components/ChordName";
import { FretDiagram, PianoDiagram } from "../components/ChordDiagram";
import { Logo } from "../components/Artwork";
import { IconPrinter } from "../components/Icons";
import { errorText } from "../components/ui";
import { useChordView, type ViewSettings } from "../player/hooks";
import { ChordLine } from "./ChordLine";
import { voicingFor } from "./voicing";
import "../styles/print.css";

/** A clean chord sheet for printing / saving as PDF: chords in bold red over black lyrics. */
export function PrintPage() {
  const { id = "" } = useParams();
  const [params, setParams] = useSearchParams();
  const [record, setRecord] = useState<SongRecord | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    api.song(id).then((r) => setRecord(r.song)).catch((e) => setError(errorText(e)));
  }, [id]);

  // the sheet is always printed light, in the usual colours
  useEffect(() => {
    const root = document.documentElement;
    const prev = { theme: root.dataset.theme, palette: root.dataset.palette };
    root.dataset.theme = "light";
    root.dataset.palette = "gold";
    document.body.classList.add("print-body");
    return () => {
      root.dataset.theme = prev.theme;
      root.dataset.palette = prev.palette;
      document.body.classList.remove("print-body");
    };
  }, []);

  const settings: ViewSettings = {
    transpose: parseInt(params.get("t") ?? "0", 10) || 0,
    capo: parseInt(params.get("c") ?? "0", 10) || 0,
    instrument: (["guitar", "ukulele", "piano", "mandolin", "bass"].includes(params.get("i") ?? "") ? params.get("i") : "guitar") as Instrument,
    simplify: params.get("s") === "1",
    capoShapes: params.get("sh") !== "0",
  };
  const columns = params.get("cols") === "2";

  if (error) return <div className="print-page"><p>{error}</p><Link to="/">Back</Link></div>;
  if (!record) return <div className="print-page"><p>Preparing the sheet…</p></div>;
  return (
    <Sheet
      record={record}
      settings={settings}
      columns={columns}
      autoPrint={params.get("print") === "1"}
      onOption={(k, v) => {
        const next = new URLSearchParams(params);
        next.set(k, v);
        next.delete("print");
        setParams(next, { replace: true });
      }}
    />
  );
}

function Sheet({
  record,
  settings,
  columns,
  autoPrint,
  onOption,
}: {
  record: SongRecord;
  settings: ViewSettings;
  columns: boolean;
  autoPrint: boolean;
  onOption: (key: string, value: string) => void;
}) {
  const doc = record.doc;
  const view = useChordView(doc, settings);

  useEffect(() => {
    document.title = `${doc.title} – chord sheet`;
    if (autoPrint) {
      const t = setTimeout(() => window.print(), 600);
      return () => clearTimeout(t);
    }
  }, [doc.title, autoPrint]);

  const legend = useMemo(() => {
    const seen = new Map<string, string>();
    for (const c of doc.chords) {
      const p = parseChord(c.name);
      if (!p || p.category === "none") continue;
      const shown = view.shown(c.name);
      if (!seen.has(shown)) seen.set(shown, c.name);
    }
    return [...seen.entries()];
  }, [doc.chords, view]);

  const firstLine = doc.lines[0]?.beat ?? Infinity;
  const intro = doc.chords.map((c, i) => ({ c, i })).filter(({ c }) => c.beat < firstLine);
  const instName = settings.instrument === "piano" ? "Piano" : FRETTED[settings.instrument].name;

  return (
    <div className="print-page">
      <div className="print-toolbar no-print">
        <Link to={`/song/${record.id}`}>← Back to the song</Link>
        <label>
          Diagrams for{" "}
          <select value={settings.instrument} onChange={(e) => onOption("i", e.target.value)}>
            <option value="guitar">Guitar</option>
            <option value="ukulele">Ukulele</option>
            <option value="piano">Piano</option>
            <option value="mandolin">Mandolin</option>
            <option value="bass">Bass</option>
          </select>
        </label>
        <label>
          <input type="checkbox" checked={columns} onChange={(e) => onOption("cols", e.target.checked ? "2" : "1")} /> Two columns
        </label>
        <button className="btn btn-primary btn-sm" onClick={() => window.print()}>
          <IconPrinter size={18} /> Print / Save as PDF
        </button>
      </div>

      <header className="sheet-head">
        <div>
          <h1>{doc.title || "Untitled"}</h1>
          {doc.artist && <div className="sheet-artist">by {doc.artist}</div>}
        </div>
        <div className="sheet-brand"><Logo compact /> SongAlong</div>
      </header>
      <div className="sheet-meta">
        <span><b>Key</b> {prettyAccidentals(view.keyName)}</span>
        <span>
          <b>Capo</b>{" "}
          {view.usesCapo
            ? `${settings.capo}${view.shapeKeyName ? ` (play ${prettyAccidentals(view.shapeKeyName)} shapes)` : ""}`
            : "None"}
        </span>
        <span><b>BPM</b> {Math.round(doc.bpm)}</span>
        <span><b>Time</b> {doc.beatsPerBar}/4</span>
        {settings.instrument !== "piano" && <span><b>Tuning</b> {FRETTED[settings.instrument].tuningNames.join(" ")}</span>}
        {settings.transpose !== 0 && <span><b>Transposed</b> {settings.transpose > 0 ? "+" : ""}{settings.transpose}</span>}
      </div>

      <section className="sheet-legend">
        <h2>Chords ({instName})</h2>
        <div className="legend-grid">
          {legend.map(([shown, original]) => {
            const v = voicingFor(original, view, settings, 0);
            return (
              <div key={shown} className={"legend-item" + (v?.kind === "piano" ? " piano" : "")}>
                <div className="legend-name"><ChordName symbol={shown} /></div>
                <CategoryTag symbol={view.sounding(original)} />
                {v?.kind === "fret" && <FretDiagram voicing={v.voicing} strings={v.strings} size={v.strings > 4 ? 96 : 84} />}
                {v?.kind === "piano" && <PianoDiagram voicing={v.voicing} size={150} />}
              </div>
            );
          })}
        </div>
      </section>

      <section className={"sheet-body" + (columns ? " two-col" : "")}>
        {intro.length > 0 && (
          <div className="sheet-block">
            <div className="sheet-section">Intro</div>
            <div className="sheet-line">
              <ChordLine text="" chords={intro.map(({ c, i }) => ({ name: view.shown(c.name), col: i, key: i }))} />
            </div>
          </div>
        )}
        {groupSections(doc.lines.length, (i) => !!doc.lines[i].section).map((block) => (
          <div className="sheet-block" key={block[0]}>
            {doc.lines[block[0]].section && <div className="sheet-section">{doc.lines[block[0]].section}</div>}
            {block.map((i) => {
              const line = doc.lines[i];
              const placed = chordsInLine(doc, i)
                .filter((p) => !p.carried || !line.text)
                .map((p) => ({ name: view.shown(p.chord.name), col: p.col, key: p.index }));
              const gap = gapAfterLine(doc, i);
              return (
                <div key={i}>
                  <div className="sheet-line">
                    <ChordLine text={line.text} chords={placed} />
                  </div>
                  {gap && (
                    <div className="sheet-line">
                      <ChordLine
                        text=""
                        chords={gap.chords.filter((p) => !p.carried).map((p, k) => ({ name: view.shown(p.chord.name), col: k, key: p.index }))}
                      />
                    </div>
                  )}
                </div>
              );
            })}
          </div>
        ))}
      </section>
      {doc.lead && doc.lead.length > 0 && (
        <section className="sheet-lead">
          <div className="sheet-section">Lead part</div>
          <p className="sheet-lead-note">Notes with octave (middle C = C4) and length in beats; r = rest.</p>
          <pre>
            {prettyAccidentals(
              exportLead(
                doc.lead.map((e) => ({ ...e, name: view.sounding(e.name) })),
                doc.beatsPerBar,
              ),
            )}
          </pre>
        </section>
      )}
      <footer className="sheet-foot">Printed from SongAlong · {new Date().toLocaleDateString()}</footer>
    </div>
  );
}

/** Line indexes grouped into sections (a new group starts at every section heading). */
function groupSections(count: number, startsSection: (i: number) => boolean): number[][] {
  const out: number[][] = [];
  for (let i = 0; i < count; i++) {
    if (!out.length || startsSection(i)) out.push([i]);
    else out[out.length - 1].push(i);
  }
  return out;
}
