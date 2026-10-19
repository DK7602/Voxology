import { useState } from "react";
import { Link } from "react-router-dom";
import { chordNoteNames, parseChord } from "../../shared/music/chords";
import { FRETTED, fretVoicings, pianoMidi, pianoVoicing, voicingMidi } from "../../shared/music/voicings";
import { prettyAccidentals } from "../../shared/music/notes";
import type { Instrument } from "../../shared/types";
import { useUser } from "../auth";
import { CategoryTag, ChordName } from "../components/ChordName";
import { FretDiagram, PianoDiagram } from "../components/ChordDiagram";
import { IconPiano, IconPlus, IconSpeaker, IconTuner } from "../components/Icons";
import { arpeggio, strum, unlockAudio } from "../player/synth";
import { Tuner } from "../tools/Tuner";

export function ToolsPage() {
  return (
    <div className="panel page-panel">
      <div className="page-head">
        <h1>Tools</h1>
      </div>
      <div className="quick-actions" style={{ gridTemplateColumns: "repeat(auto-fill, minmax(240px, 1fr))" }}>
        <Link className="quick-action" to="/new">
          <IconPlus size={30} />
          <b>Add a song</b>
          <span>Recording, YouTube or chord sheet</span>
        </Link>
        <Link className="quick-action" to="/tools/chords">
          <IconPiano size={30} />
          <b>Chord library</b>
          <span>Any chord on any instrument</span>
        </Link>
        <Link className="quick-action" to="/tools/tuner">
          <IconTuner size={30} />
          <b>Tuner</b>
          <span>Uses your microphone</span>
        </Link>
      </div>
    </div>
  );
}

export function TunerPage() {
  const user = useUser();
  const [inst, setInst] = useState<Exclude<Instrument, "piano">>(
    user.settings.instrument && user.settings.instrument !== "piano" ? user.settings.instrument : "guitar",
  );
  return (
    <div className="panel page-panel" style={{ maxWidth: 720, margin: "0 auto" }}>
      <div className="page-head">
        <h1>Tuner</h1>
        <div className="seg" role="group" aria-label="Instrument">
          {(["guitar", "ukulele", "mandolin", "bass"] as const).map((i) => (
            <button key={i} aria-pressed={inst === i} onClick={() => setInst(i)}>
              {FRETTED[i].name}
            </button>
          ))}
        </div>
      </div>
      <Tuner key={inst} instrument={inst} />
    </div>
  );
}

const ROOTS = ["C", "C#", "Db", "D", "Eb", "E", "F", "F#", "Gb", "G", "Ab", "A", "Bb", "B"];
const TYPES = [
  ["", "Major"], ["m", "Minor"], ["5", "Power"], ["7", "7"], ["maj7", "maj7"], ["m7", "m7"], ["sus2", "sus2"], ["sus4", "sus4"],
  ["add9", "add9"], ["6", "6"], ["m6", "m6"], ["9", "9"], ["m9", "m9"], ["dim", "dim"], ["dim7", "dim7"], ["m7b5", "m7♭5"],
  ["aug", "aug"], ["7sus4", "7sus4"], ["11", "11"], ["13", "13"],
] as const;

export function ChordLibraryPage() {
  const user = useUser();
  const [root, setRoot] = useState("C");
  const [type, setType] = useState<string>("");
  const [bass, setBass] = useState("");
  const [inst, setInst] = useState<Instrument>(user.settings.instrument ?? "guitar");
  const symbol = root + type + (bass && bass !== root ? "/" + bass : "");
  const chord = parseChord(symbol);

  const play = async (midis: number[]) => {
    const ctx = await unlockAudio();
    strum(midis, ctx.currentTime + 0.02, { instrument: inst, velocity: 0.9, spread: 0.035 });
  };

  return (
    <div className="panel page-panel">
      <div className="page-head">
        <h1>Chord library</h1>
        <div className="seg" role="group" aria-label="Instrument">
          {(["guitar", "ukulele", "piano", "mandolin", "bass"] as Instrument[]).map((i) => (
            <button key={i} aria-pressed={inst === i} onClick={() => setInst(i)}>
              {i[0].toUpperCase() + i.slice(1)}
            </button>
          ))}
        </div>
      </div>
      <div style={{ display: "grid", gap: 10 }}>
        <div className="seg" role="group" aria-label="Root">
          {ROOTS.map((r) => (
            <button key={r} aria-pressed={root === r} onClick={() => setRoot(r)}>
              {prettyAccidentals(r)}
            </button>
          ))}
        </div>
        <div className="seg" role="group" aria-label="Chord type">
          {TYPES.map(([t, label]) => (
            <button key={t} aria-pressed={type === t} onClick={() => setType(t)}>
              {label}
            </button>
          ))}
        </div>
        <label className="check">
          Bass note (slash chord):
          <select className="select" style={{ width: 120, minHeight: 38 }} value={bass} onChange={(e) => setBass(e.target.value)}>
            <option value="">none</option>
            {ROOTS.map((r) => (
              <option key={r} value={r}>{prettyAccidentals(r)}</option>
            ))}
          </select>
        </label>
      </div>

      {chord && (
        <div style={{ display: "flex", alignItems: "baseline", gap: 14, flexWrap: "wrap", margin: "22px 0 12px" }}>
          <span style={{ fontSize: 44 }}><ChordName symbol={symbol} /></span>
          <CategoryTag symbol={symbol} />
          <span className="muted">
            {chord.description} · notes {chordNoteNames(chord).map(prettyAccidentals).join(" ")} · formula {chord.degrees.join(" ")}
          </span>
        </div>
      )}

      <div className="grid-cards" style={{ gridTemplateColumns: "repeat(auto-fill, minmax(170px, 1fr))" }}>
        {inst === "piano"
          ? (() => {
              const v = pianoVoicing(symbol);
              return v ? (
                <div className="diagram-card piano" style={{ width: "auto", gridColumn: "span 2" }}>
                  <PianoDiagram voicing={v} size={220} />
                  <button className="btn btn-sm" onClick={() => play(pianoMidi(v))}><IconSpeaker size={18} /> Play</button>
                </div>
              ) : null;
            })()
          : fretVoicings(symbol, inst, 12).map((v, i) => (
              <div key={i} className="diagram-card" style={{ width: "auto" }}>
                <span className="faint" style={{ fontSize: 14 }}>{i === 0 ? "Easiest" : `Option ${i + 1}`}</span>
                <FretDiagram voicing={v} strings={FRETTED[inst].tuning.length} leftHanded={!!user.settings.leftHanded} size={140} />
                <button
                  className="btn btn-sm"
                  onClick={async () => {
                    const midis = voicingMidi(v, inst);
                    if (v.order || inst === "bass") {
                      const ctx = await unlockAudio();
                      arpeggio(midis, ctx.currentTime + 0.02, 0.3, inst);
                    } else void play(midis);
                  }}
                >
                  <IconSpeaker size={18} /> Play
                </button>
              </div>
            ))}
      </div>
    </div>
  );
}
