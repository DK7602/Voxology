import { Link } from "react-router-dom";
import type { SongSummary } from "../../shared/api";
import type { SongDoc } from "../../shared/types";
import { prettyAccidentals } from "../../shared/music/notes";
import { FRETTED } from "../../shared/music/voicings";
import { Artwork } from "../components/Artwork";
import { ChordName } from "../components/ChordName";
import {
  IconArrowRight,
  IconCapo,
  IconHeadphones,
  IconMore,
  IconNote,
  IconPiano,
  IconPrinter,
  IconTranspose,
  IconTuner,
  IconWrench,
} from "../components/Icons";
import { Popover } from "../components/ui";
import type { ChordView, ViewSettings } from "../player/hooks";

export function SongDetails({ doc, view, settings, chords }: { doc: SongDoc; view: ChordView; settings: ViewSettings; chords: string[] }) {
  const tuning =
    settings.instrument === "piano"
      ? "—"
      : `Standard (${FRETTED[settings.instrument].tuningNames.join(" ")})`;
  return (
    <div className="panel side-panel">
      <div className="panel-title">
        <IconNote size={24} /> Song Details
      </div>
      <dl className="details">
        <dt>Chords</dt>
        <dd>
          {chords.map((c) => (
            <ChordName key={c} symbol={c} />
          ))}
        </dd>
        <dt>Key</dt>
        <dd>
          {prettyAccidentals(view.keyName)}
          {view.shapeKeyName && <span className="faint">(shapes in {prettyAccidentals(view.shapeKeyName)})</span>}
        </dd>
        <dt>BPM</dt>
        <dd>{Math.round(doc.bpm)}</dd>
        <dt>Time</dt>
        <dd>{doc.beatsPerBar}/4</dd>
        <dt>Capo</dt>
        <dd>{view.usesCapo ? settings.capo : settings.instrument === "piano" || settings.instrument === "bass" ? "—" : "None"}</dd>
        {doc.genre && (
          <>
            <dt>Genre</dt>
            <dd>{doc.genre}</dd>
          </>
        )}
        <dt>Tuning</dt>
        <dd>{tuning}</dd>
      </dl>
    </div>
  );
}

export function ToolsPanel({
  simplify,
  onTranspose,
  onCapo,
  onTuner,
  onMidi,
  onPrint,
  onSimplify,
}: {
  simplify: boolean;
  onTranspose: () => void;
  onCapo: () => void;
  onTuner: () => void;
  onMidi: () => void;
  onPrint: () => void;
  onSimplify: () => void;
}) {
  const tiles = [
    { icon: IconTranspose, title: "Transpose", sub: "Change key easily", on: false, act: onTranspose },
    { icon: IconCapo, title: "Capo", sub: "Find the best position", on: false, act: onCapo },
    { icon: IconTuner, title: "Tuner", sub: "Tune your instrument", on: false, act: onTuner },
    { icon: IconPiano, title: "MIDI Export", sub: "Export to MIDI file", on: false, act: onMidi },
    { icon: IconPrinter, title: "Print Chords", sub: "Get a clean sheet", on: false, act: onPrint },
    { icon: IconNote, title: "Simplify Chords", sub: simplify ? "On: easier versions" : "Easier versions", on: simplify, act: onSimplify },
  ];
  return (
    <div className="panel side-panel">
      <div className="panel-title">
        <IconWrench size={24} /> Tools &amp; Features
      </div>
      <div className="tools-grid">
        {tiles.map(({ icon: Icon, title, sub, on, act }) => (
          <button key={title} className={"tool-tile" + (on ? " on" : "")} onClick={act} aria-pressed={title.startsWith("Simplify") ? on : undefined}>
            <Icon />
            <b>{title}</b>
            <span>{sub}</span>
          </button>
        ))}
      </div>
    </div>
  );
}

export function SimilarSongs({ songs }: { songs: SongSummary[] }) {
  return (
    <div className="panel side-panel">
      <div className="panel-title" style={{ justifyContent: "space-between" }}>
        <span style={{ display: "inline-flex", gap: 10, alignItems: "center" }}>
          <IconHeadphones size={24} /> Similar Songs
        </span>
        <Link to="/discover" style={{ fontSize: 16, fontFamily: "EB Garamond, serif", fontWeight: 400, display: "inline-flex", gap: 6, alignItems: "center" }}>
          See all <IconArrowRight size={18} />
        </Link>
      </div>
      <div className="similar-list">
        {songs.length === 0 && <div className="faint" style={{ padding: "8px 0" }}>Play more songs to see matches here.</div>}
        {songs.map((s) => (
          <div key={s.id} className="song-row">
            <Link to={`/song/${s.id}`} style={{ display: "contents", color: "inherit", textDecoration: "none" }}>
              <Artwork title={s.title} youtubeId={s.youtubeId} rounded={8} />
              <div style={{ minWidth: 0 }}>
                <div className="title">
                  {s.artist ? `${s.artist} - ` : ""}
                  {s.title}
                </div>
                <div className="sub">
                  {s.plays} jam session{s.plays === 1 ? "" : "s"} · chords: {s.chords.slice(0, 4).map(prettyAccidentals).join(" ")}
                </div>
              </div>
            </Link>
            <Popover
              align="end"
              width={180}
              trigger={(p) => (
                <button className="btn-ghost" style={{ border: 0, background: "none", cursor: "pointer", color: "inherit" }} {...p} ref={p.ref} aria-label="More">
                  <IconMore size={22} />
                </button>
              )}
            >
              {() => (
                <Link className="menu-item" to={`/song/${s.id}`}>
                  Open
                </Link>
              )}
            </Popover>
          </div>
        ))}
      </div>
    </div>
  );
}
