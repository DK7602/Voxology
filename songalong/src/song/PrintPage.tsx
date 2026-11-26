import { useEffect, useLayoutEffect, useMemo, useRef, useState, type CSSProperties } from "react";
import { Link, useParams, useSearchParams } from "react-router-dom";
import type { SongRecord } from "../../shared/api";
import type { Instrument, SongDoc } from "../../shared/types";
import { parseChord } from "../../shared/music/chords";
import { prettyAccidentals } from "../../shared/music/notes";
import { FRETTED, isFretted } from "../../shared/music/voicings";
import { chordsInLine, gapAfterLine } from "../../shared/timeline";
import { exportLead } from "../../shared/lead";
import { api } from "../api";
import { ChordName, CategoryTag } from "../components/ChordName";
import { FretDiagram, PianoDiagram } from "../components/ChordDiagram";
import { Logo } from "../components/Artwork";
import { IconPrinter } from "../components/Icons";
import { errorText } from "../components/ui";
import { useChordView, type ChordView, type ViewSettings } from "../player/hooks";
import { ChordLine } from "./ChordLine";
import { chooseFit, COLUMN_GAP, PAGE, pagesFor } from "./printFit";
import { voicingFor } from "./voicing";
import "../styles/print.css";

/**
 * A clean chord sheet for printing / saving as PDF: chords in bold red over black lyrics. Page 1
 * has the song's details and its chord diagrams; the words and chords start on page 2, under the
 * title and details again, sized to fit on as few pages as read well. The lead part is optional.
 */
export function PrintPage() {
  const { id = "" } = useParams();
  const [params, setParams] = useSearchParams();
  const [record, setRecord] = useState<SongRecord | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    api.song(id).then((r) => setRecord(r.song)).catch((e) => setError(errorText(e)));
  }, [id]);

  // the sheet is always printed light, in the usual colors
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
    chordStyle: params.get("cs") === "full" || params.get("cs") === "power" ? (params.get("cs") as "full" | "power") : "recorded",
    capoShapes: params.get("sh") !== "0",
  };
  // two columns unless one is asked for: twice the words and chords on a page
  const columns = params.get("cols") !== "1";

  if (error) return <div className="print-page"><p>{error}</p><Link to="/">Back</Link></div>;
  if (!record) return <div className="print-page"><p>Preparing the sheet…</p></div>;
  return (
    <Sheet
      record={record}
      settings={settings}
      columns={columns}
      lead={params.get("lead") === "1"}
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
  lead,
  autoPrint,
  onOption,
}: {
  record: SongRecord;
  settings: ViewSettings;
  columns: boolean;
  lead: boolean;
  autoPrint: boolean;
  onOption: (key: string, value: string) => void;
}) {
  const doc = record.doc;
  const view = useChordView(doc, settings);
  const measure = useRef<HTMLDivElement>(null);
  const [fit, setFit] = useState<{ scale: number; pages: number } | null>(null);
  const settingsKey = JSON.stringify(settings);

  // size the words and chords for the printed page: measured off-screen at the page's width
  useLayoutEffect(() => {
    let gone = false;
    void document.fonts.ready.then(() => {
      const box = measure.current;
      const body = box?.querySelector<HTMLElement>(".sheet-body");
      const foot = box?.querySelector<HTMLElement>(".sheet-foot");
      if (gone || !box || !body || !foot) return;
      const next = chooseFit((scale) => {
        box.style.setProperty("--fit", String(scale));
        const head = body.getBoundingClientRect().top - box.getBoundingClientRect().top;
        // each piece's height is the step to the next piece's top: the gap between them included
        const starts = [...[...body.children].flatMap(pieceStarts), foot];
        const tops = starts.map((e) => e.getBoundingClientRect().top);
        const end = foot.getBoundingClientRect().bottom;
        const blocks = tops.map((t, i) => (i + 1 < tops.length ? tops[i + 1] : end) - t);
        return pagesFor(blocks, columns ? 2 : 1, head);
      });
      setFit((f) => (f && f.scale === next.scale && f.pages === next.pages ? f : next));
    });
    return () => {
      gone = true;
    };
  }, [doc, settingsKey, columns]);

  useEffect(() => {
    document.title = `${doc.title} – chord sheet`;
    if (autoPrint && fit) {
      const t = setTimeout(() => window.print(), 400);
      return () => clearTimeout(t);
    }
  }, [doc.title, autoPrint, fit]);

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

  const instName = settings.instrument === "piano" ? "Piano" : settings.instrument === "cajon" ? "Cajon" : FRETTED[settings.instrument].name;
  const hasLead = !!doc.lead && doc.lead.length > 0;
  const foot = <footer className="sheet-foot">Printed from SongAlong · {new Date().toLocaleDateString()}</footer>;

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
        {hasLead && (
          <label>
            <input type="checkbox" checked={lead} onChange={(e) => onOption("lead", e.target.checked ? "1" : "0")} /> Lead part
          </label>
        )}
        {fit && (
          <span className="print-fit">
            Words &amp; chords: {fit.pages} {fit.pages === 1 ? "page" : "pages"}
            {fit.scale < 1 && ` (text at ${Math.round(fit.scale * 100)}%)`}
          </span>
        )}
        <button className="btn btn-primary btn-sm" onClick={() => window.print()}>
          <IconPrinter size={18} /> Print / Save as PDF
        </button>
      </div>

      {legend.length > 0 && (
        <section className="sheet-first">
          <SheetTop doc={doc} view={view} settings={settings} />
          <section className="sheet-legend">
            <h2>Chords ({instName})</h2>
            <div className="legend-grid">
              {legend.map(([shown, original]) => {
                const v = voicingFor(original, view, settings, 0);
                return (
                  <div key={shown} className={"legend-item" + (v?.kind === "piano" ? " piano" : "")}>
                    <div className="legend-name"><ChordName symbol={shown} /></div>
                    <CategoryTag symbol={view.sounding(original)} filledBy={view.filledBy(original)} />
                    {v?.kind === "fret" && <FretDiagram voicing={v.voicing} strings={v.strings} size={v.strings > 4 ? 96 : 84} />}
                    {v?.kind === "piano" && <PianoDiagram voicing={v.voicing} size={150} />}
                  </div>
                );
              })}
            </div>
          </section>
        </section>
      )}

      <section className={"sheet-words" + (legend.length > 0 ? " new-page" : "")} style={{ "--fit": fit?.scale ?? 1 } as CSSProperties}>
        <SheetTop doc={doc} view={view} settings={settings} compact />
        <div className={"sheet-body" + (columns ? " two-col" : "")}>
          <Words doc={doc} view={view} />
        </div>
        {foot}
      </section>

      {lead && hasLead && (
        <section className="sheet-lead">
          <div className="sheet-section">Lead part</div>
          <p className="sheet-lead-note">Notes with octave (middle C = C4) and length in beats; r = rest.</p>
          <pre>
            {prettyAccidentals(
              exportLead(
                doc.lead!.map((e) => ({ ...e, name: view.sounding(e.name) })),
                doc.beatsPerBar,
              ),
            )}
          </pre>
        </section>
      )}

      {/* the words page as printed, out of sight, for measuring (two columns measured one column wide) */}
      <div className="print-page print-measure no-print" ref={measure} aria-hidden="true" style={{ width: PAGE.width }}>
        <SheetTop doc={doc} view={view} settings={settings} compact />
        <div className="sheet-body" style={{ width: columns ? (PAGE.width - COLUMN_GAP) / 2 : PAGE.width }}>
          <Words doc={doc} view={view} />
        </div>
        {foot}
      </div>
    </div>
  );
}

/** A section of up to this many lines is kept together on a page; a longer one (or a song with no
 *  sections, all one block) may break between its lines. */
const KEEP_LINES = 8;

/** Where each piece of a block of the words begins, the pieces printing whole: a short section all
 *  together; a long one line by line, its heading with its first line. */
function pieceStarts(block: Element): Element[] {
  if (block.classList.contains("keep")) return [block];
  const parts = [...block.children];
  return parts[0]?.classList.contains("sheet-section") ? [parts[0], ...parts.slice(2)] : parts;
}

/** The title, artist and the song's details, at the top of the first page and of the words. */
function SheetTop({ doc, view, settings, compact }: { doc: SongDoc; view: ChordView; settings: ViewSettings; compact?: boolean }) {
  return (
    <div className={"sheet-top" + (compact ? " compact" : "")}>
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
        {isFretted(settings.instrument) && <span><b>Tuning</b> {FRETTED[settings.instrument].tuningNames.join(" ")}</span>}
        {settings.transpose !== 0 && <span><b>Transposed</b> {settings.transpose > 0 ? "+" : ""}{settings.transpose}</span>}
      </div>
    </div>
  );
}

/** The intro's chords, then each section of lines with its chords over the words. */
function Words({ doc, view }: { doc: SongDoc; view: ChordView }) {
  const firstLine = doc.lines[0]?.beat ?? Infinity;
  const intro = doc.chords.map((c, i) => ({ c, i })).filter(({ c }) => c.beat < firstLine);
  return (
    <>
      {intro.length > 0 && (
        <div className="sheet-block keep">
          <div className="sheet-section">Intro</div>
          <div className="sheet-line">
            <ChordLine text="" chords={intro.map(({ c, i }) => ({ name: view.shown(c.name), col: i, key: i }))} />
          </div>
        </div>
      )}
      {groupSections(doc.lines.length, (i) => !!doc.lines[i].section).map((block) => (
        <div className={"sheet-block" + (block.length <= KEEP_LINES ? " keep" : "")} key={block[0]}>
          {doc.lines[block[0]].section && <div className="sheet-section">{doc.lines[block[0]].section}</div>}
          {block.map((i) => {
            const line = doc.lines[i];
            const placed = chordsInLine(doc, i)
              .filter((p) => !p.carried || !line.text)
              .map((p) => ({ name: view.shown(p.chord.name), col: p.col, key: p.index }));
            const gap = gapAfterLine(doc, i);
            return (
              <div key={i} className="sheet-pair">
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
    </>
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
